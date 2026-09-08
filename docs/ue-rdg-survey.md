# UE 5.8.1 RDG Survey (Stage 4 Design Reference)

Status: **research note** — not a contract. This feeds S4.1's `docs/rdg.md` and
ADR-003 (the RDG boundary decision). It records how Unreal Engine's render
dependency graph is designed, what RenderLab's mini-RDG should adopt, and where
the two deliberately diverge.

Source tree: `G:\UnrealEngine` (UE 5.8.1, per `Engine/Build/Build/Build.version`;
read-only reference, not part of this repository). All facts below carry
`file:line` references into that tree so every claim can be re-checked.

## 0. Scope filter

Read against RenderLab's plan (`IMPLEMENTATION_PLAN.md` §8–§9), this survey
covers: handles and resource identity, descriptors, pass records and access
declaration, dependency edges, culling, lifetimes, dumps, validation, and (as
an S5.3/S5.7 preview) barriers and pooling.

Deliberately **out of scope** for RenderLab (plan §2.4), recorded here only so
ADR-003 can cite what we are not adopting: async compute / multi-queue
scheduling, parallel setup/compile/execute, render-pass merging, uniform
buffers and the parameter-struct macro machinery, the blackboard, the RHI
transient allocator, RDG Insights (trace UI), subresource-granular state
tracking, and memory budgeting.

## 1. Architecture overview

The entry point is `FRDGBuilder`, stack-scoped: passes are appended with
`AddPass` during setup, then `Execute()` compiles and executes everything
(`RenderGraphBuilder.h:45-49`). Sentinel passes bracket the graph — a
"Graph Prologue (Graphics)" pass is created in the constructor
(`RenderGraphBuilder.cpp:598`) and an epilogue pass at the top of `Execute()`
(`:1793`); the prologue handle is always 0 (`RenderGraphBuilder.h:485-488`).

`Execute()` (`RenderGraphBuilder.cpp:1766-2234`) in order: flush pending
transient deallocations → create epilogue sentinel → wait for parallel setup
tasks → `FinalizeDescs` → `PrepareCollectResources` → **`Compile()`**
(`:1327-1674`) → compile/collect pass barriers → collect allocations and
deallocations per non-culled pass → allocate pooled/transient resources →
create views and uniform buffers → **serial execution loop**
(`:2104-2167`, `if (Pass->bCulled) continue;`) → fill extraction pointers →
post-execute callbacks → trace end.

Three load-bearing architectural facts:

1. **UE's RDG is monolithic.** One component owns the logical graph, the
   physical resource state, allocation, barriers, and parallel execution.
   RenderLab's plan splits this deliberately: Stage 4 is a pure logical CPU
   compiler (no GPU, no NVRHI headers), Stage 5 is the execution layer.
2. **There is no scheduler.** 5.8 executes passes in **insertion order**
   (`RenderGraphBuilder.cpp:1962-1971`); dependency edges feed culling and
   async-compute fencing, not reordering. (Dependency levels /
   `ParallelExecuteFor` from UE 4.24-era RDG no longer exist — grep over
   RenderCore returns zero hits. Parallelism in 5.8 is "contiguous pass sets"
   submitted as tasks, `SetupParallelExecute` at `:2841-3066`.)
3. **Parameters are evaluated at AddPass time**, not at Execute. The parameter
   struct pointer supplied to `AddPass` is walked immediately
   (`RenderGraphBuilder.cpp:2384-2532`, via layout-table reflection). Only the
   execute lambda is deferred.

File map (RenderCore, `Engine/Source/Runtime/RenderCore/`):

| File | Contents |
|---|---|
| `Public/RenderGraphDefinitions.h` | `TRDGHandle`, `ERDGPassFlags`, resource flags, builder flags |
| `Public/RenderGraphBuilder.h/.inl` | `FRDGBuilder` public API (Create/Register/AddPass/Extract) |
| `Private/RenderGraphBuilder.cpp` (~173 KB) | the builder: setup, compile, culling, barriers, allocation, execution |
| `Public/RenderGraphPass.h`, `Private/RenderGraphPass.cpp` | `FRDGPass`, lambda-pass template, barrier batches |
| `Public/RenderGraphResources.h/.inl` | `FRDGResource`, `FRDGTexture`, `FRDGBuffer`, subresource states |
| `Public/RenderGraphParameter.h`, `Public/ShaderParameterMacros.h` | parameter-struct reflection and the `RDG_*_ACCESS` macros |
| `Private/RenderGraphValidation.cpp` | debug validation (§9) |
| `Private/RenderGraphTrace.cpp` | UE Trace events (§8) |
| `Private/RenderGraphResourcePool.h/.cpp` | buffer pool + transient adapter (§7) |
| `Engine/Source/Runtime/RHI/Public/RHIAccess.h` | `ERHIAccess` and its masks |

## 2. Handles and resource identity

**There is no `FRDGResourceHandle` in 5.8.1** (grep over `Engine/Source`:
zero hits; it was a UE 4.22-era packed index+type word). The internal handle is
a template over an index only (`RenderGraphDefinitions.h:340-436`):

```cpp
template <typename LocalObjectType, typename LocalIndexType>
class TRDGHandle
{
    ...
private:
    static const IndexType kNullIndex = TNumericLimits<IndexType>::Max();
    IndexType Index = kNullIndex;
};
```

- Concrete: `FRDGPassHandle`, `FRDGTextureHandle`, `FRDGBufferHandle`, ... =
    `TRDGHandle<Obj, uint32>` (`RenderGraphDefinitions.h:761-782`). Null is a
    sentinel (`NumericLimits::Max()`); handles are ordered by index (operators
    `<=`, `++`, `Min`/`Max` — used for pass ranges and first/last bookkeeping).
- **No generation/epoch counter exists** (grep `generation|epoch` over the RDG
    headers: zero hits).
- The **public API passes raw pointers**: `using FRDGTextureRef = FRDGTexture*`
    (`RenderGraphFwd.h:38-39`). Handles are builder-internal indices into
    handle registries (`TRDGHandleRegistry`, `Definitions.h:446-579`).
- Stale/dangling use is caught by **debug validation, not by the handle**:
    a `ResourceMap.Contains(Resource)` checkf in `ValidateAddPass`
    (`RenderGraphValidation.cpp:658-661`, "not part of the graph and is likely
    a dangling pointer or garbage value"), an `ExecuteGuard` checkf for any
    post-Execute operation (`:80-83`), and a single-active-builder check
    (`:68-73`).

**Implication for RenderLab:** our plan's typed handles with generation
checks are *stronger* than UE's mechanism, and that is a deliberate divergence
(see §10). What we should take from UE: the null-sentinel pattern, index
ordering for first/last-pass intervals, and per-object-type handle templates.

## 3. Resources: create, register, extract

`FRDGResource` (`RenderGraphResources.h:133-193`) is a name plus a lazily
assigned `FRHIResource*`. The intermediate `FRDGViewableResource`
(`:292-497`) carries all graph bookkeeping:

- `ReferenceCount` — **builder-internal**; there are no user-facing
  `AddRef`/`Release` calls anywhere. Incremented per parameter binding during
  setup, accumulated in `SetupPassDependencies`
  (`RenderGraphBuilder.cpp:2338, 2363`), subtracted for culled passes
  (`:1407-1417`); `ReferenceCount == 0` is exactly "resource is culled"
  (`FRDGTexture::IsCulled`, `RenderGraphResources.h:622-625`).
- Pass-range fields: `AcquirePass`, `DiscardPass`, `FirstPass`,
  `LastPasses` (per pipeline), `RenderGraphResources.h:439-442`.
- Ownership bitfields: `bTransient`, `bExternal`, `bExtracted`, `bProduced`,
  `bForceNonTransient`, ... (`:377-415`).
- `IsCullRoot() = bExternal || bExtracted` (`:356-359`).

The three ways a resource enters/leaves the graph:

| API | What it does | Cull root? | GPU memory |
|---|---|---|---|
| `CreateTexture` / `CreateBuffer` (`RenderGraphBuilder.inl:41-100`) | allocates only the CPU object from the frame allocator; validates the desc; **no RHI resource, no ref count** | no | allocated lazily by the graph at first use; returned to the pool at last use |
| `RegisterExternalTexture/Buffer` (`RenderGraphBuilder.cpp:1097-1156`) | dedups by RHI pointer, assigns the existing RHI, sets `bExternal`, holds a strong ref | yes (`bExternal`) | owned outside the graph |
| `QueueTextureExtraction` / `QueueBufferExtraction` (`RenderGraphBuilder.inl:447-499`; renamed from `ExtractTexture` in older UE) | sets `bExtracted`, queues the output pointer (filled at the very end of `Execute()`, `Builder.cpp:2202-2212`) | yes (`bExtracted`) | lifetime extended to the epilogue; transient eligibility disabled by default |

Why extraction exists (`Builder.inl:447-475` + `Builder.cpp:1373-1381`): it
extends GPU lifetime past the last pass, makes the producer chain a culling
root, and hands the pooled allocation out to the caller. Extracting a
never-produced resource is a hard validation failure
(`RenderGraphValidation.cpp:423-430`).

**There is no per-write versioning.** Grep for `Version` across the RDG
headers returns zero hits: a write does not return a new handle — the same
`FRDGTexture*` is the single logical resource for the whole graph; WAW is
handled by insertion order plus per-subresource access state plus
last-producer edges (§5). RenderLab's plan (S4.2) deliberately diverges here.

Descriptors: `FRDGTextureDesc : FRHITextureDesc` with static factories
(`RenderGraphDefinitions.h:628-742`); `FRDGBufferDesc` with
`BytesPerElement`/`NumElements`/`Usage` and an `operator==`
(`RenderGraphResources.h:938-1118`) — that equality is the pooling key (§7).

Lazy acquisition: created resources start with `ResourceRHI == nullptr` and
`bCollectForAllocate = 1`; during compile, the first pass that begins each
resource queues the allocate op (`Builder.cpp:3648-3753`), executed by
`AllocatePooledTextures/Buffers` or `AllocateTransientResources`
(`:3128-3313`). Reading a resource never written is a debug `ensureMsgf`
warning ("has a read dependency on %s, but it was never written to",
`RenderGraphValidation.cpp:651-656`), not a hard error.

## 4. Passes and access declaration

`ERDGPassFlags` verbatim (`RenderGraphDefinitions.h:128-161`): `None`,
`Raster`, `Compute`, `AsyncCompute`, `Copy`, `NeverCull` ("Pass (and its
producers) will never be culled. Necessary if outputs cannot be tracked by
the graph."), `SkipRenderPass`, `NeverMerge`, `NeverParallel`, and
`Readback = Copy | NeverCull`. Note: **UE has no separate "side-effect" flag —
`NeverCull` is the side-effect flag**, and the parameterless `AddPass`
implicitly ORs it in (`RenderGraphBuilder.inl:283`).

`FRDGPass` (`RenderGraphPass.h:217-564`) holds: `Name` (an `FRDGEventName`),
`ParameterStruct`, `Flags`, `TaskMode`, derived `Pipeline` (Graphics unless
`AsyncCompute`, `RenderGraphPass.cpp:449`), `Handle`, `bCulled : 1`,
`bHasExternalOutputs`, immediate dependency lists
(`Producers`, `CrossPipelineProducer`, `CrossPipelineConsumers`),
prologue/epilogue barrier pass handles, and per-pass resource state
(`TextureStates`/`BufferStates` with per-subresource `State`, `MergeState`,
`ReferenceCount`).

Read/write declaration. There is **no explicit read/write flag** — it is
encoded in the `ERHIAccess` value:

```cpp
// RHI/Public/RHIAccess.h:81
WritableMask = WriteOnlyMask | UAVMask | BVHWrite;   // WriteOnlyMask = RTV | CopyDest | ResolveDst | DSVWrite
```

`IsWritableAccess(Access)` (`RHIAccess.h:103-106`) is what the builder uses to
know a pass *writes* a resource — that sets `bProduced`
(`RenderGraphBuilder.cpp:2451-2460`), updates last-producer edges (§5), and
feeds culling. Plain texture/SRV/UAV parameters derive their access from the
pass flags via `GetPassAccess` (`:64-85`): e.g. an SRV on a `Raster` pass is
`SRVGraphics`, a UAV on a `Compute` pass is `UAVCompute`.

The access enum itself (`RHIAccess.h:10-86`) is worth reading in full — its
shape (individual read/write states, pipeline folded into the bits, then
derived masks `ReadOnlyExclusiveMask`, `WritableMask`, `ReadableMask`, ...)
is the reference design for our S5.3 access subset. Per-resource flags
(`ERDGTextureFlags`/`ERDGBufferFlags`, `RenderGraphDefinitions.h:163-208`)
are orthogonal: `MultiFrame`, `SkipTracking`, `ForceImmediateFirstBarrier`,
plus `MaintainCompression` for textures.

Parameter declaration macros (`ShaderParameterMacros.h:1866-1885`):

```cpp
#define RDG_TEXTURE_ACCESS(MemberName, Access) \
    INTERNAL_SHADER_PARAMETER_EXPLICIT(UBMT_RDG_TEXTURE_ACCESS, \
        TRDGTextureAccessTypeInfo<TRDGTextureAccess<Access>>, ...)
```

This declares a member of `TRDGTextureAccess<ERHIAccess::...>` — the access is
baked into the *type* — inside a `BEGIN_SHADER_PARAMETER_STRUCT` block, and
registers it in the struct's static layout metadata. The runtime struct
(`ShaderParameterMacros.h:366-415`) is `{ Texture*, SubresourceRange, Access }`.
Traversal is by layout tables (`RenderGraphParameter.h:263-277`), recursively
through uniform buffers. This whole macro/reflection machinery is exactly the
part RenderLab will **not** adopt (it exists to bind shader parameters; our
mini-RDG declares reads/writes on pass records directly).

`AddPass` signatures (`RenderGraphBuilder.h:220-240`): typed
`(name, ParameterStruct*, flags, lambda)`, dynamic-metadata, and parameterless
(forces `NeverCull`). The execute lambda receives only a command list (the
`(params*, cmdlist)` form is deprecated 5.5); the parameter struct pointer is
captured by the user. A public manual escape hatch exists:
`AddPassDependency(Producer, Consumer)` ("fine-tune async compute overlap",
`RenderGraphBuilder.h:248-249`).

## 5. Compile: dependency edges and culling

**Edges are last-producer links, not full consumer lists.** During
`SetupPassDependencies` (`RenderGraphBuilder.cpp:2329-2382`) /
`AddCullingDependency` (`:1199-1276`), every resource keeps per-pipeline
`LastProducers`; a consumer gets one edge
`AddPassDependency(LastProducerPass, NextState.Pass)` per pipeline, and only a
**write** updates the last-producer state. Intermediate consumers are
compressed away — "immediate dependency" style. `AddPassDependency` dedupes
via `Producers.Find` and maintains cross-pipeline consumer lists for
async-compute fencing (`:1160-1197`).

**Culling is incremental and interleaved with setup, not a separate phase.**
Every pass is marked `bCulled = GRDGCullPasses > 0` by default
(`RenderGraphBuilder.cpp:2373-2374`); when a *root* is discovered, a DFS
immediately un-culls its producer closure:

```cpp
// RenderGraphBuilder.cpp:1310-1323
void FRDGBuilder::FlushCullStack()
{
    while (CullPassStack.Num())
    {
        FRDGPass* Pass = CullPassStack.Pop(EAllowShrinking::No);
        if (Pass->bCulled)
        {
            Pass->bCulled = 0;
            CullPassStack.Append(Pass->Producers);
        }
    }
}
```

Roots (`:2376`): a pass that writes a cull-root resource
(`bExternal || bExtracted`), a pass with `bHasExternalOutputs`, or a pass with
`NeverCull`. Prologue/epilogue sentinels are always un-culled
(`:1392-1393`). `Compile()` then only subtracts reference counts contributed
by culled passes (`:1407-1417`). The design comment at `:1383-1386` states the
model exactly: "all passes are marked as culled and a depth first search is
employed to find reachable regions of the graph. Roots of the search are
those passes with outputs leaving the graph or those marked to never cull."

Controls: global `r.RDG.CullPasses` (default on,
`RenderGraphPrivate.cpp:231-238`); per-pass `NeverCull`. **There is no
per-pass cull-reason logging** — only a stat counter and the `IsCulled` trace
flag. A pass with no graph parameters and no `NeverCull` provokes a warning at
AddPass time ("The pass will always be culled",
`RenderGraphValidation.cpp:811-812`).

## 6. Barriers and state planning (S5.3 preview)

State is tracked per subresource: `FRDGSubresourceState { Access, FirstPass,
LastPass, NoUAVBarrierFilter, Flags, BarrierLocation }`
(`RenderGraphResources.h:72-129`). `CompilePassBarriers`
(`RenderGraphBuilder.cpp:3783-3870`) merges consecutive compatible states
(`FRDGSubresourceState::IsMergeAllowed`, `RenderGraphResources.cpp:69-133` —
read-only never merges with writable; UAV merges only with itself); then
`CollectPassBarriers` (`:3872-3928`) walks non-culled passes and emits
transitions where `IsTransitionRequired`. Placement
(`AddTransition`, `:4439-4585`): a transition **begins in the epilogue of the
producer's barrier pass and ends in the prologue of the consumer's** — UE's
split-barrier scheme, with per-pass batches `PrologueBarriersToBegin/ToEnd`
and `EpilogueBarriersToBegin*` (`RenderGraphPass.h:533-544`). Graph-entry and
graph-exit transitions come from `FinalizeResources` (`:3985-4030`). Accesses
from pass parameters are unioned with `MakeValidAccess` (`:43-62`), which
strips read-only bits when the union is writable.

## 7. Lifetimes and pooling (S5.7 preview)

Within the graph: `FirstPass` / `LastPasses` per resource (per pipeline), with
`AcquirePass`/`DiscardPass` for RHI transient resources. The buffer pool
(`RenderGraphResourcePool.cpp:110-135`) keys on an *aligned* descriptor
(sizes rounded to 64 KB pages or powers of two) with full-equality collision
defeat, and reuses only when fence predicates prove the previous use's pass
range does not overlap. Pooled buffers are retained for 30 frames
(`TickPoolElements`, `:212-251`). Textures go through the global
`FRenderTargetPool` (desc-hash + full compare + fence predicate). On top of
both sits an RHI-level transient allocator (heap aliasing) with
acquire/discard barriers (`Builder.cpp:3217-3313`, `:4586-4614`).

RenderLab's S5.7 is much smaller: pool exact-compatible physical resources
whose **logical lifetimes within the frame do not overlap** — interval-based,
fence-free. The transferable ideas are the descriptor-keyed pool and
"last-used-frame" retention.

## 8. Observability

**5.8.1 has no text/dot graph dump.** `r.RDG.Dump` / `GraphDumpFilenameOrIndex`
do not exist (grep over `Engine/Source`: zero hits; older UE versions had
them). What exists is **UE Trace structured events** consumed by the
RenderGraphInsights plugin (`RenderGraphTrace.cpp`): one `GraphMessage` per
builder; one `PassMessage` per pass — including culled ones — carrying name,
handle, timings, flags, pipeline, `IsCulled`, fork/join passes, and its
texture/buffer handle lists (`:28-50`); `ScopeMessage`s with first/last pass
and depth; one `TextureMessage`/`BufferMessage` per resource carrying
`Passes[]` (every pass handle touching it — the lifetime edges),
`IsExternal/IsExtracted/IsCulled/IsTransient`, descriptor fields, and
transient allocation offsets (`:339-399`).

Pass names flow through `FRDGEventName` (`RenderGraphEvent.h:48-80`) and
scope RAII (`RDG_EVENT_SCOPE`), with first/last-pass intervals computed per
scope (`CompilePassOps`, `Builder.cpp:2668-2687`). There is also an
**immediate mode** (`r.RDG.ImmediateMode`, `RenderGraphPrivate.cpp:16-21`)
that executes each pass inline inside `AddPass` — a debugging strategy worth
remembering when we build the S5 manual/RDG switch.

The event *schemas* are the transferable design: they define "what a graph
dump should contain". Our S4.6 text/DOT dumps should carry the same
information (pass list with flags and cull state, per-resource pass lists,
scopes), just as text instead of trace events.

## 9. Validation taxonomy (S4.1 error-design reference)

Debug validation lives in `RenderGraphValidation.cpp` (guarded by
`RDG_ENABLE_DEBUG` + `r.RDG.Validation`), split into user-API misuse
(`FRDGUserValidation`) and barrier validation. The concrete checks, grouped:

- **Lifecycle**: one builder at a time (`:71`); execute before destruction
  (`:77`); no API use after Execute (`:82`); execute only once (`:923`).
- **Creation**: null names; zero-byte buffers (`:118`); UAV on a texture
  without `TexCreate_UAV` (`:299`); flag/desc contradictions (`:225-260`).
- **Extraction/external**: extract of never-produced resource (`:427`);
  `GetPooledTexture` on a non-external resource (`:484`); conversion after
  execution began (`:440`).
- **Access declaration**: incompatible access states on the same subresource
  within one pass (`:583`); access/pipeline mismatched with pass flags
  (`:672-675`); flag combinations (Raster+AsyncCompute exclusive `:617`,
  SkipRenderPass requires Raster `:620`).
- **Provenance**: cross-graph / dangling resource pointer
  (`ResourceMap.Contains`, `:660`); read of a never-written resource
  (`ensureMsgf`, `:653`); pass that can never produce and lacks `NeverCull`
  (`:811`).
- **Execution-time**: touching a resource's RHI outside the pass that declared
  it (`GetRHI()` guarded by `bAllowRHIAccess`, `RenderGraphResources.h:153`
  + `RenderGraphValidation.cpp:36-47`) — the check that enforces
  "undeclared lookup" at execution time.
- **Render-target binding**: packed contiguity (`:915`), targetable flags
  (`:856-896`).

RenderLab's S4.1/S4.6 should mirror the *provenance* and *access-declaration*
categories first (they map to the plan's "stale, null, cross-graph,
type-mismatched handles fail deterministically"), and treat UE's
execution-time `GetRHI` guard as the model for S5.1's "undeclared lookup
fails before command recording".

## 10. What RenderLab adopts, diverges, and skips

The plan's design already differs from UE in four deliberate ways; ADR-003
should cite this table.

| Area | UE 5.8.1 | RenderLab plan | Verdict |
|---|---|---|---|
| Public resource identity | raw pointers + debug validation; index-only internal handles, no generation | typed handles with generation checks; stale/null/cross-graph/type-mismatch fail deterministically | **diverge (ours stronger)** — we are CPU-only and can afford it; take UE's null-sentinel + index-ordering + typed handle templates |
| Logical versioning | none; WAW via insertion order + last-producer edges + subresource state | every write creates a new version; superseded-use rejected (S4.2) | **diverge** — classic FrameGraph model; buys deterministic diagnostics and cycle/read-before-produce detection UE cannot express |
| Scheduling | none; linear insertion-order execution | topo sort with stable tie-break; cycle diagnostics (S4.3) | **diverge (modest)** — with a stable tie-break, valid graphs keep submission order; we gain cycle/read-before-produce errors |
| Cull-reason reporting | none (counter + trace flag only) | record cull reason per pass (S4.4) | **diverge (ours richer)** — algorithm shape (mark-all + backward DFS from roots) is adopted wholesale |
| Graph dump | removed in 5.x; UE Trace events + RDG Insights | stable text + DOT dumps, golden tests (S4.6) | **adopt the content, not the format** — the trace event schemas define the dump content checklist |
| Read/write encoding | access-enum only (`WritableMask`) | explicit read/write declarations (S4.1) | **diverge** — our compiler layer has no RHI; an explicit read/write bit is the honest logical model. Take the enum+mask *shape* for the S5.3 access subset |
| Import/export | `bExternal` / `bExtracted`; `IsCullRoot = either` | imported/exported flags (S4.1) | **adopt** the same semantics and the cull-root rule |
| Pass flags | 10 flags incl. merging/parallel/async | minimal: raster + side-effect/never-cull (+ later DXR) | **subset** |
| Edge structure | last-producer compression, cross-pipeline fences, manual `AddPassDependency` | RAW/WAR/WAW edges from versions, dedup | adopt compression + dedup; skip cross-pipeline and manual override initially |
| Culling roots | external/extracted producers, external outputs, NeverCull | outputs/side-effects as roots (S4.4) | **adopt** |
| Validation | checkf/ensureMsgf taxonomy (§9) | deterministic Debug/test failures with named pass+resource | **adopt the categories**, not the macro machinery |
| Pooling/transients | cross-frame pools with fences + RHI transient allocator | within-frame interval reuse (S5.7) | subset; adopt descriptor-key idea |
| Uniform buffers, blackboard, parallel setup/execute, async compute, render-pass merging, subresource states, RDG Insights | exists | excluded by plan §2.4 | **skip** — ADR-003 cites this row |

## 11. Open questions for S4.1 (to confirm before implementation)

1. **Handle representation**: pack `{index, generation}` (and type via C++
   template, UE-style) into one word, or separate fields? The plan requires
   generation checks; UE's answer (no generation, debug validation only)
   works because pointers are the public API there — ours handles are public,
   so generation is the staleness mechanism.
2. **Error reporting**: UE mixes fatal `checkf` (lifecycle, provenance) and
   non-fatal `ensureMsgf` (read-before-write). RenderLab's S4.6 wants
   "compile error categories" with pass+resource names — collect-and-report
   (like our comparator's error taxonomy) rather than immediate abort?
3. **Pass flags subset**: is `Raster | Compute | Copy | NeverCull` (+ a
   readback alias?) the right starting set, or do we defer Compute/Copy until
   a pass needs them (the M1 pipeline is raster + present only)?
4. **Access declaration**: explicit per-declaration read/write bit plus an
   optional logical usage hint (RenderTarget/DepthWrite/SRV/UAV/Copy), with
   the full state enum deferred to S5.3? (Keeps Stage 4 NVRHI-free.)
5. **Where does the version live**: on the handle (so a stale version is
   detectable at use) or in a side table keyed by handle (UE's side-table
   instinct, but then staleness needs validation, not the type system)?

These questions do not block writing `docs/rdg.md`'s scope section or
ADR-003's boundary; they are the first grill round of S4.1.
