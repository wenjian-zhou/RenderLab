# RDG Model and Boundary Contract

Status: **active design note — Stage 5** (implemented through S5.7; Stage 5 / M2 is satisfied). The current resource identity and barrier model is [ADR-005](adr/ADR-005-rdg-ue-resource-identity.md).

Step: S5.7, then ADR-005

One texture or buffer has one handle (`index`, `graphId`). Passes declare
`Use(handle, Access)`. Order is `AddPass` order. Edges run from the last
earlier writer to a later reader. Initial and final access are explicit.
`GraphExecutor::Execute` issues the barriers. Collected errors, public
`Compile()`, cull reasons, text/DOT dumps, S5.7 reuse, and pass `Execute`
owning `setGraphicsState` are unchanged. The sections below that describe
versioned handles, `Read` / `Write`, RAW/WAR/WAW edges, and topological
order are the Stage 4–S5.7 record; ADR-005 replaces that identity model.

This file records the mini RDG's logical model: handles, resource
descriptors, pass records, read/write declarations, version provenance,
dependency edges, topological order, output-driven pass culling, logical
resource lifetimes, and compiled-graph text/DOT dumps. S5.1 adds the
execution layer that binds imported handles to opaque physical tokens.
S5.2 maps descriptors to NVRHI and allocates internal Create* identities.
S5.3 plans raster access states from those declarations. S5.4 schedules
the existing `PostProcessPass` through a one-pass imported leaf. S5.5
schedules deferred lighting then tone map through `BuildLightingPresentGraph`
(Create* HDR, first production `Allocate`). S5.6 gives the whole raster
frame to one graph (`BuildRasterFrameGraph`): GBuffer, depth, and HDR are
Create*+Allocate, and one `GraphExecutor::Execute` runs the live passes.
S5.7 aliases exact-compatible Create* resources whose live-slot intervals
do not overlap, inside one `Allocate()`, and reports reuse pairs plus
logical and physical byte totals.
How this design relates to UE 5.8.1's render
dependency graph — what is adopted,
diverged, and skipped, with `file:line` citations — lives in
[`ue-rdg-survey.md`](ue-rdg-survey.md) and
[`adr/ADR-003-rdg-boundary.md`](adr/ADR-003-rdg-boundary.md),
[`adr/ADR-004-rdg-single-module.md`](adr/ADR-004-rdg-single-module.md), and
[`adr/ADR-005-rdg-ue-resource-identity.md`](adr/ADR-005-rdg-ue-resource-identity.md); this file does
not repeat those arguments. Renderer terminology follows
[`renderer-conventions.md`](renderer-conventions.md). A drawn walkthrough of
the S4.2–S4.6 code — version history, the declaration flow, the per-method
field-access matrix, compilation, culling, lifetimes, and compiled dumps — is in
[`rdg-dataflow.md`](rdg-dataflow.md).

## 1. Scope and boundary

The `src/rdg` module (`RenderLab::Rdg` target, namespace `renderlab::rdg`)
owns the logical graph and the execution layer that runs it (ADR-004):

- It owns logical resource identity, descriptors, pass records, explicit
  read/write declarations, logical versions with producer/reader provenance,
  imported/exported flags, pass flags, declaration-time validation,
  RAW/WAR/WAW dependency edges, a deterministic pass order or cycle
  diagnostic, output-driven pass culling with stable reasons, logical
  first/last-use lifetimes after culling, and stable text/DOT dumps of the
  compiled graph. The same target binds imports, maps formats, allocates
  internals, plans access states, and runs pass lambdas.
- It links `RenderLab::ProjectOptions` and `nvrhi`, not donut. `GraphBuilder.h`
  forward-declares `nvrhi::ICommandList` and does not include an NVRHI header.
  `FormatMap` and `GraphExecutor.cpp` include NVRHI. Versioned handles,
  explicit read/write, collected errors, and the neutral `rdg::Format` /
  `rdg::Access` enums stay as in ADR-003.
- Tests construct graphs with **no GPU device**; `Allocate()` without
  `SetDevice` uses CPU stubs. `RenderLabDataContractTests` runs entirely on
  CPU, including the CI runners.
- `GraphBuilder::AddPass(name, flags, lambda)` stores the pass lambda.
  The no-lambda overload stays for compile-only tests and the declaration
  factories. `BuildToneMapGraph`, `BuildLightingPresentGraph`,
  `BuildRasterFrameGraph`, and `BuildM1ShapedGraph` stay no-lambda factories.
  The app calls `BuildRasterFrameGraph`, attaches lambdas with `SetLambda`,
  and fills `RasterFrameSnapshot` from `FindTexture` after `Execute`.
  `Compile()` stays a public CPU step.
  `GraphExecutor::Execute(ICommandList*)` is the only public run entry.
  S5.7 aliases non-overlapping intervals inside `Allocate()`; this file
  defines the intervals, and §10 defines the physical reuse rule.

The renderer is untouched by Stage 4. S5.4 began integration against the
frozen M1 reference ([`m1-reference.md`](m1-reference.md)). S5.6: the
production raster frame is the graph. GBuffer debug does not produce HDR.

## 2. Handles

`TextureHandle` and `BufferHandle` are plain value types with public fields:

```text
{ uint32 index; uint32 version; uint32 graphId; }   // no bit packing
```

- `index` — slot in the owning graph's **unified** resource registry
  (textures and buffers share one numbering, unlike UE's per-type
  registries; the unified registry is what makes a runtime kind check
  possible). Indices are never recycled within a graph.
- `version` — the logical-version slot. Create/import mint version 0;
  every successful `Write` appends a version and returns a new handle with
  the same index and graph identity. The handle **is** the version identity.
- `graphId` — taken from a global counter when a `GraphBuilder` is
  constructed; 0 is reserved so a default-constructed handle can never
  resolve. `graphId` is what makes cross-graph use a deterministic failure
  instead of an index+version coincidence. Multiple builders may coexist.

Null is the `kNullHandleIndex` sentinel in `index` (UE's null-sentinel
pattern); default-constructed handles are null.

The two handle types are distinct C++ classes, so honest code cannot mix a
texture with a buffer declaration at compile time. Handles are **forgeable
by design** (public fields, aggregate-initializable): the four-category
failure matrix below tests exactly the dishonest handles, and the registry
validation is the safety net. This is the deliberate divergence from UE,
whose public API passes raw pointers and relies on debug validation
(survey §2, §10 row 1).

## 3. Resource descriptors

```text
TextureDesc { name, width, height, format }
BufferDesc  { name, bytesPerElement, numElements }
```

Both are value types with `operator==`, which includes the debug name.
S5.7's reuse key ignores that name. Textures match on format, width, and
height. Buffers match on `bytesPerElement` and `numElements` (a shared byte
size with a different stride does not match). UE precedent for a descriptor
key: `FRDGBufferDesc::operator==`.

`Format` is a **neutral owned enum** (`SRGBA8Unorm`, `RGBA8Unorm`,
`RGBA16Float`, `RGBA32Float`, `R32Float`, `D32Float`), covering the frozen
M1 pipeline resources. S5.2 owns the mapping table from these values to
`nvrhi::Format`; Stage 4 never names an RHI type. Extend the enum only
when a real resource needs a new value.

A resource enters the graph three ways, mirroring UE's semantics (survey
§3, §10 "Import/export" row):

| API | Flag | Meaning |
|---|---|---|
| `CreateTexture` / `CreateBuffer` | — | graph-internal logical allocation |
| `ImportTexture` / `ImportBuffer` | `Imported` | physical counterpart exists outside the graph (UE `bExternal`); in S4.1 the descriptor describes it for S5.1 mapping |
| `ExportTexture` / `ExportBuffer` | `Exported` | output leaves the graph (UE `bExtracted`); pins the current produced version |

`imported || exported` marks a cull root. A pass that writes such a resource
is a culling root. An imported resource may also be written (back-buffer
precedent).

### Version semantics (S4.2)

`ResourceRecord::versions` is append-only, indexed by handle version.
`ResourceVersionRecord` stores `produced`, `producerPass`, and `readerPasses`:

| Version | `produced` | `producerPass` | Legal first uses |
|---|---|---|---|
| Internal v0 | false | `Error::kNoPass` | Write; Read and Export fail |
| Imported v0 | true | `Error::kNoPass` (external producer) | Read, Write, Export |
| Written v1+ | true | exactly one declaring pass index | Read, next-pass Write, Export |

`Write` is a whole-resource overwrite declaration, not an implicit read of
the previous contents. The predecessor is version `n - 1`; its producer and
readers remain available for S4.3 WAW/WAR edges. No physical copy or allocation
is implied. Every successful write records an access to the **output** version.
Callers must retain the returned handle: `texture = pass.Write(texture)`.
Rejected writes (including on an invalid `PassBuilder`) return a null handle.

Only the current version may be used by a new Read, Write, or Export call.
After a write, older handles are superseded; declarations accepted earlier
remain valid and retain their original version. A stale write cannot branch
the version history. Version changes follow declaration-call order, not pass
index order: interleaved builders are supported, and an older pass may read
a version already produced by a newer pass. S4.3 derives scheduling edges
from this history; a graph that declares without errors can still be
unschedulable if those edges form a cycle.

A pass may read a resource repeatedly, but cannot both read and write it,
nor write it twice (even using the returned current handle). This deliberately
excludes same-pass in-place read/modify/write and attachment-load semantics
until the API has an explicit contract for them. Distinct resources can be
read and written in the same pass. Repeated reads remain in the access list;
`readerPasses` contains unique pass indices in first-read declaration order.

Export requires a produced current version and is idempotent. It pins that
version as the final output: further reads are allowed, but later writes are
rejected. This prevents an exported handle from silently naming overwritten
contents and lets the resource-level `exported` flag unambiguously identify
`currentVersion`. Export adds no synthetic reader pass.

## 4. Passes and declarations

`GraphBuilder::AddPass(name, flags)` appends a `PassRecord` and returns a
`PassBuilder` — a value holding the builder and the pass index (never a
pointer into storage that can reallocate). Declarations hang off the pass:

```cpp
auto pass = builder.AddPass("DeferredLighting", PassFlags::Raster);
pass.Read(gbufferA);
hdrSceneColor = pass.Write(hdrSceneColor);
```

- `PassFlags` starts as `None | Raster | NeverCull`. `Raster` is still
  printed by S4.6 dumps; S5.3 infers access from Read/Write + format and
  does not require the flag (every live pass access is planned).
  `NeverCull` is the side-effect flag (UE precedent — survey §4): the pass
  and its last-producer closure survive culling. Unknown bits are rejected.
  `Compute`/`Copy`/`Readback` are added when a real pass needs them.
- Accepted reads pin the input version; accepted writes pin the new output
  version. Same-resource conflicts follow the S4.2 rules above.
- Pass and resource names must be non-empty; duplicates are allowed.
- `PassRecord` stays name, flags, and accesses, so the compiler never
  inspects a `std::function`. S4.3 compiles declarations into a pass order;
  S4.4 culls passes that cannot affect an exported resource or declared side
  effect. `GraphBuilder::AddPass(name, flags, lambda)` stores a
  `std::function<void(nvrhi::ICommandList*, PassContext&)>` in a side table
  at that pass index, and only when the pass was actually added. The
  no-lambda overload does not store a function. The command list is a
  pointer so device-free tests can pass nullptr. `GraphExecutor::Execute`
  is the only public run entry: it walks the compiled live order, skips
  culled passes, and calls the stored lambda. A live pass with no lambda is
  `InvalidPass` and stops later passes. A culled pass is not invoked.
  Lookup errors recorded inside a lambda do not stop the walk.

## 5. Failure taxonomy

Every mutating call (`Create*`, `Import*`, `AddPass`, `Read`, `Write`,
`Export*`) validates immediately. A rejected call records one or more
structured errors and leaves the graph unchanged by that call. Execution-time
lookup (`RegisterImport`, `GetExported`, `GetTexture` / `GetBuffer`,
`Allocate`, `Plan`) uses the
same `Error` values. Errors are
**collected, never thrown**, and never implicitly asserted — the explicit
`GraphBuilder::AssertNoErrors()` / `GraphExecutor::AssertNoErrors()` Debug
traps are for non-test callers. Compile dumps (S4.6) print each error's
category plus pass and resource names.

```text
Error { ErrorCategory category; std::string message;
        uint32 passIndex; std::string passName; std::string resourceName; }
```

Naming both the pass and the resource is part of the contract from day one.

| Category | Trigger |
|---|---|
| `NullHandle` | a null handle used in a declaration or export |
| `ForeignGraph` | handle's `graphId` is not this builder's |
| `StaleVersion` | forged future version, or out-of-range index on the owning graph |
| `SupersededUse` | Read, Write, or Export names an older version |
| `ReadBeforeProduce` | Read or Export names an unproduced internal v0 |
| `DuplicateWrite` | same pass writes the same resource again using its current handle |
| `IncompatibleAccess` | same-pass read/write combination, Write after Export, RegisterImport of a non-imported or already-bound resource, GetExported of a non-exported resource, a second `Allocate()`, a second `Plan()`, or `SetDevice` after `Allocate()` |
| `TypeMismatch` | handle kind does not match the registry slot's kind (only reachable with a forged handle) |
| `InvalidName` | empty pass or descriptor name |
| `InvalidDescriptor` | zero extent, unknown texture format, zero element size/count, or `Allocate()` of an unmapped format |
| `InvalidPassFlags` | pass flags outside the known bit set |
| `ZeroUseAllocation` | compile-time: a non-imported resource has no live-pass access after culling |
| `UndeclaredAccess` | `GetTexture` / `GetBuffer` names a resource that exists on the graph but is not in this pass's read/write list |
| `ExpiredContext` | `Get*` after that pass's lambda has returned (`Execute` deactivates the context) |
| `UnregisteredImport` | declared or exported handle has no physical token (imports before `RegisterImport`; internals before `Allocate()`) |
| `InvalidPass` | `Execute()` on a failed compile, or a live pass with no lambda (later passes are not run). A culled pass is skipped and is not `InvalidPass`. Also `Allocate()` / `Plan()` after a failed compile |
| `AllocationFailed` | `nvrhi::IDevice::createTexture` / `createBuffer` returned null |
| `UnknownAccess` | `Plan()` cannot map a live access (buffer write; `Access::Unknown` or combined bits) |

Validation order is deterministic — when a forged handle violates several
rules at once, the earliest category wins:

```text
null → owning graph → index range → kind → version
```

Handle validation precedes access validation. Thus repeating a write with
the original superseded handle reports `SupersededUse`; repeating it with
the returned current handle reports `DuplicateWrite`. For a valid handle,
same-pass conflicts precede produce/export checks. Rejected calls change
only the error collection, never versions, accesses, readers, or export flags.

## 6. Usage conventions

- `GraphBuilder` is stack-scoped and non-copyable; pass `PassBuilder`
  values around freely, but they are valid only while their builder lives.
- A `PassBuilder` returned from a rejected `AddPass` is invalid
  (`IsValid() == false`); its `Read`/`Write` are no-ops, so a partially
  rejected graph remains explorable.
- Declarations may be interleaved across passes; each `PassBuilder` keeps
  targeting its own pass record.
- Single-threaded declaration recording, like the rest of the renderer's
  frame loop; the global `graphId` counter is the only shared state.

## 7. Verification

`tests/test_rdg_handles.cpp` covers graph identity, minted
handle fields, the four-category failure matrix at every entry point
(read/write/export), validation order, and repeated-read recording.
`tests/test_rdg_declarations.cpp` covers descriptor validation,
import/export flag semantics, descriptor equality, pass flag validation,
per-declaration rejection isolation, and an M1-shaped graph — six
resources and three passes mirroring
[`m1-reference.md`](m1-reference.md) §3 — constructed with no GPU device.

`tests/test_rdg_versioning.cpp` runs the same semantic cases for textures
and buffers: single writer, imported-to-exported read chain, multiple readers,
WAW with historical provenance, read-before-write and export-before-write,
real superseded handles at all three entry points, forged future versions,
duplicate/incompatible writes, both mixed-access orders, pinned export, and
rejection isolation. It also asserts the textual provenance dump.

`tests/test_rdg_compiler.cpp` covers empty graphs, RAW reordering when the
consumer is declared first, RAW/WAR/WAW edge directions on an overwrite
fan-out/fan-in, independent passes in min-index topological order, merged
multi-reason edges, intentional cycles with named diagnostics and no partial
order, builder-error short-circuit, and deterministic `CompileResult::Dump`
text.

`tests/test_rdg_culling.cpp` covers unused leaf and unused chain (now
compile failures with `ZeroUseAllocation` after culling), exported
and imported output roots, `NeverCull` producer closure, shared producers,
global disable-culling, WAR-not-walked overwrite, deterministic dump, and
error/cycle short-circuit with `cull: skipped` and `lifetime: skipped`.

`tests/test_rdg_lifetimes.cpp` covers first/last pass indices versus live
slots, overlapping and non-overlapping intervals, imported last-access
extent, unused imported (including no error), exported epilogue extent,
culled zero-use, mixed live-plus-unused failure without partial lifetimes,
read-only imported v0, disable-culling as a live use, deterministic dump,
and error/cycle `lifetime: skipped`.

`tests/test_rdg_dump.cpp` covers the M1-shaped golden (`tests/golden/rdg/rdg.txt`
and `rdg.dot`), dump determinism, cycle and builder-error dumps that name pass
and resource, DOT live vs culled, and ZeroUse dumps that keep `cull:` plus
`ZeroUseAllocation` with `lifetime: skipped`.

`tests/test_rdg_exec.cpp` covers two-phase import bind, `GetExported`,
declared `GetTexture` / `GetBuffer` during `Execute`, undeclared /
stale / null / foreign / type-mismatch / expired lookup, unbound internals,
skipped culled passes, `Execute` on a failed compile, imported buffers, and
the M1-shaped BackBuffer rule (only PostProcess may resolve it). No GPU device.

`tests/test_rdg_alloc.cpp` covers `FormatMap` round-trips for every
`rdg::Format`, `Allocate()` of internal textures and buffers, imported
slots remaining `RegisterImport`-only, failed-compile and second
`Allocate()` errors, M1-shaped GBuffer/HDR resolve without a device, create/
destroy of repeated graphs, `GetAllocationStats`, and `SetDevice` after
`Allocate()`. CI never creates a D3D12 device.

`tests/test_rdg_access.cpp` covers `rdg::Access` flags and masks,
`AccessMap` enumerator mapping to `nvrhi::ResourceStates`, FormatMap
initial-state agreement, M1-shaped before/after/restore including imported
BackBuffer `Present`, repeated read keeping `ShaderResource`, culled-pass
omission, buffer-write `UnknownAccess`, failed-compile and second `Plan()`,
and `Execute` without `Plan()`. CI never creates a D3D12 device.

`tests/test_rdg_raster.cpp` covers the three `RasterPresent` live orders,
GBufferDebug with no HDR resource, AccessPlan rows, device-free `Allocate`
byte counts, lambda order matching `GetLivePassOrder`, skipped culled
passes, a missing lambda as `InvalidPass`, and `UnregisteredImport` of
BackBuffer. No GPU device.

## 8. Version dump

`GraphBuilder::DumpVersions()` returns deterministic text in resource-index,
then version order. Each row names the resource kind/index/name, version,
producer (pass index/name, `external`, or `unproduced`), reader indices/names,
and current/exported status. It omits graph IDs so identical declarations
produce identical text across builders. This is S4.2 provenance evidence.
S4.3–S4.6's compiler dump is separate (`CompileResult::Dump` / `DumpDot`) and
does not require the builder to still be alive.

```text
texture 0 'Input' v0 producer=external readers=[0 'First'] current
texture 1 'Intermediate' v0 producer=unproduced readers=[]
texture 1 'Intermediate' v1 producer=0 'First' readers=[1 'Second'] current
texture 2 'Output' v0 producer=unproduced readers=[]
texture 2 'Output' v1 producer=1 'Second' readers=[2 'Third'] current exported
```

## 9. Graph compiler (S4.3–S4.6)

`GraphCompiler::Compile(const GraphBuilder&, CompileOptions = {})` is a pure
function. It copies pass/resource names, pass flags, per-pass accesses,
resource kinds, imported/exported flags, version provenance, and builder
errors into a `CompileResult` and never retains the builder. `IsSuccess()` is
true only when the error list is empty and no cycle was found.

If the builder already recorded declaration errors, compilation copies them
and returns immediately: no edges, no pass order, no cycle diagnostic, no
cull state, no lifetimes (`cull: skipped` and `lifetime: skipped` in the dump).

Otherwise every resource version contributes ordering edges:

| Type | Direction | When |
|---|---|---|
| RAW | producer of vn → each reader of vn | a pass reads a produced version |
| WAR | each reader of v(n-1) → producer of vn | a later pass overwrites a version that had readers |
| WAW | producer of v(n-1) → producer of vn | a later pass overwrites a produced version |

`Error::kNoPass` producers (internal unproduced v0, imported v0) do not emit
edges. Reasons on the same pass pair merge into one edge, then sort and
deduplicate by resource index, versions, and type. Independent passes still
appear in the order.

The scheduler is Kahn's algorithm with a min-heap of ready pass indices:
when several passes have indegree zero, the lowest index runs first. A
valid graph therefore has one legal order. If not every pass is scheduled,
the partial order is discarded and a deterministic DFS records a cycle of
pass names plus one dependency reason per step. Failed compiles invent no
cull or lifetime state.

After a successful order, culling runs as a separate logical step (UE
mark-all + last-producer DFS, survey §5). `GetPassOrder()` keeps the full
topological list. `GetLivePassOrder()` is that list filtered to un-culled
passes. `GetPassCullStates()` is one record per pass in index order.

- Default: every pass starts culled. Roots are a pass with
  `PassFlags::NeverCull` (`root-never-cull`, wins over output) or a `Write`
  to a resource with `imported || exported` (`root-output`). From each root,
  walk **incoming RAW and WAW** edges and un-cull the producer closure
  (`producer`). WAR is scheduling-only and does not keep unused previous
  readers. Remaining passes are `unused-chain` if they have an outgoing
  RAW/WAW edge, else `unused-leaf`.
- `CompileOptions::disableCulling` keeps every pass live with reason
  `culling-disabled` (analog of UE `r.RDG.CullPasses=0`).

After a successful cull, lifetimes run as a second logical step. They walk
`GetLivePassOrder()` only — never the full edge list, which still contains
WAR/WAW into culled passes — and read each live pass's recorded accesses
from the builder during `Compile`. `CompileResult` stores owned interval
vectors plus the dump snapshot listed above; it does not retain the builder.

```text
VersionLifetime  { resourceIndex, version, firstPass, lastPass }
ResourceLifetime { resourceIndex, firstPass, lastPass, imported, exported }
```

`firstPass` / `lastPass` are GraphBuilder pass indices of the earliest and
latest live-schedule access (read or write). Writes record the output
version, so an overwrite is not an access of `v(n-1)`. Unproduced internal
v0 is never emitted. Because declaration index is not schedule order,
`firstPass` may be numerically greater than `lastPass`. Two resources
overlap iff those pass indices map to overlapping closed slots on
`GetLivePassOrder()`.

Resource interval is min(first) / max(last) across that resource's live
versions on the live schedule, then:

- **Imported:** live from graph entry (`GetLivePassOrder().front()`) through
  last live access. Unused imported — including imported v0 that is
  immediately overwritten, so v0 has no access — is still a live resource:
  pin unused imported v0 at graph entry; do not reject. Empty live schedule
  uses `Error::kNoPass` for both ends. Imported last-use is **not** extended
  to the last live pass (unlike export).
- **Exported:** last use extends through the last live pass (UE extract →
  epilogue). The producer of the pinned exported version is already live via
  S4.4. If imported and exported, import wins `first` and export wins `last`.
- **Zero-use:** a non-imported resource with no live-pass access is
  `ZeroUseAllocation` (one error per resource, index order,
  `passIndex = Error::kNoPass`). That is a compile failure: lifetime vectors
  stay empty. Cull state is kept (`lifetime: skipped` after the `cull:`
  section). Imported unused is not an error. Disable-culling makes every
  writer a live use, so it does not trigger zero-use.

`GetVersionLifetimes()` is sorted by resource index then version.
`GetResourceLifetimes()` is sorted by resource index.

`CompileResult::Dump()` is stable text: compile status, the ordered pass
list, each edge with its reasons, copied builder errors, `cycle: none` or
the named cycle, then `cull:` (or `cull: skipped`) and `lifetime:` (or
`lifetime: skipped`), then `flags:`, `accesses:`, and `versions:` from the
snapshot. `flags:` / `accesses:` / `versions:` always emit; they are never
skipped. `Error::kNoPass` prints as `none`. Resource lifetime rows may end
with ` imported` and/or ` exported`. Identical declarations dump identically
across builders (no `graphId`).

```text
compile: success
order: [1 "Producer", 0 "Consumer"]
edges:
  1 "Producer" -> 0 "Consumer"
    RAW resource 0 "Resource" v1 -> v1
errors:
cycle: none
cull:
  0 "Consumer" live root-never-cull
  1 "Producer" live producer
lifetime:
  resource 0 "Resource" first=1 last=0
  version 0 "Resource" v1 first=1 last=0
flags:
  0 "Consumer" Raster NeverCull
  1 "Producer" Raster
accesses:
  0 "Consumer"
    read texture 0 "Resource" v1
  1 "Producer"
    write texture 0 "Resource" v1
versions:
  texture 0 "Resource"
    v0 producer=unproduced readers=[]
    v1 producer=1 "Producer" readers=[0 "Consumer"]
```

`CompileResult::DumpDot()` is a second stable Graphviz `digraph RDG`. Pass
nodes use ids `p{index}` and labels of index, quoted name, flags, and
`live`/`culled` plus cull reason when culling ran. Each dependency reason is
one edge labeled `RAW|WAR|WAW "resource" v{source}`. Resources live in
`subgraph cluster_resources` (id `r{index}`): imported/exported flags always,
`first=`/`last=` only when lifetimes were applied (same pass indices as
`order:` / `cull:`; `none` for `kNoPass`). No addresses, timestamps, or
colors. CI does not run Graphviz; committed `tests/golden/rdg/rdg.dot` is the
renderable proof. Locally: `dot -Tpng rdg.dot -o rdg.png`.

`--dump-rdg <dir>` writes the M1-shaped representative graph's `rdg.txt` and
`rdg.dot` and exits before creating a device.

## 10. Execution layer (S5.1–S5.7)

`GraphBuilder::ImportTexture({desc})` stays logical-only. After compile,
`renderlab::rdg::GraphExecutor` binds imports, allocates internals,
and plans access states:

```text
PhysicalTexture / PhysicalBuffer { void* native; std::string debugName; }
GraphExecutor::SetDevice(nvrhi::IDevice*)   // optional; default null
GraphExecutor::Allocate()
GraphExecutor::Plan()
GraphExecutor::GetAllocationStats()
GraphExecutor::GetAccessPlan()
GraphExecutor::RegisterImport(handle, physical)
GraphExecutor::GetExported(handle)
GraphExecutor::Execute(nvrhi::ICommandList*)
PassContext::GetTexture / GetBuffer
```

- `rdg::Access` is a neutral flag enum in `RenderLabRdg` (`Unknown`,
  `ShaderResource`, `RenderTarget`, `DepthWrite`, `Present`) with
  `kKnownAccessBits`, `kWritableMask` (`RenderTarget | DepthWrite`), and
  `kReadableMask` (`ShaderResource`). `Present` is a graph-boundary state,
  not inferred from Read/Write. UAV is deferred until a real pass needs it.
- `AccessMap` maps a single known `Access` bit to `nvrhi::ResourceStates`
  (`ToNvStates` / `FromNvStates` / `IsSingleKnownAccess`) and infers pass
  access from existing declarations: Read → `ShaderResource`; Write of
  `D32Float` → `DepthWrite`; Write of any other known color format →
  `RenderTarget`; buffer Write → `Unknown`.
- `FormatMap` (`ToNvFormat`, `MakeTextureDesc`, `MakeBufferDesc`) is CPU-only
  and needs no device. Usage flags come from format: `D32Float` is typeless
  depth; every other known format is color RT+SRV. `keepInitialState` stays
  `true` — that is the one NVRHI path. S5.3 declares the implied
  before/after plan; it does not call `setTextureState` or record commands.
  S5.4 consumes `Plan()` as the audit of NVRHI auto-tracking on imported
  HDR (RT) and imported+exported BackBuffer (Present). Restores happen at
  command-list close because those textures already have
  `keepInitialState = true`. S5.4 does not call `setTextureState` or
  `beginTracking` (the HDR-capture readback `beginTracking` stays on its
  own command list).
- `Plan()` is a one-shot CPU walk of live passes after a successful compile.
  It does not require `Allocate()`, `SetDevice()`, or `RegisterImport()`.
  Culled passes are omitted. A second `Plan()` is `IncompatibleAccess`.
  Failed compile is `InvalidPass`. Unmapped live access is `UnknownAccess`
  and leaves the plan empty. `Execute()` does not require `Plan()`.
- Imported initial/final convention: internals match FormatMap
  `initialState` as `rdg::Access` (color `RenderTarget`, `D32Float`
  `DepthWrite`, buffers `ShaderResource`); imported+exported textures
  (BackBuffer) are `Present`; imported-only textures use FormatMap from the
  logical descriptor. Final equals initial. `keepInitialState` restore-at-
  close is recorded as `restore` rows when last-use `after` differs from
  `final`.
- `AccessPlan::Dump()` explains every planned raster state request using
  `rdg::Access` names. `CompileResult::Dump()` / `--dump-rdg` stay
  logical-only. PIX agreement is local, not a ctest.
- `Allocate()` mints physical identities for non-imported `Create*` slots
  after a successful compile. Imported slots are skipped. A second
  `Allocate()` is `IncompatibleAccess`. Failed compile is `InvalidPass` and
  fills nothing. S5.7 may alias a later `Create*` onto an earlier physical
  object; see the reuse rules below.
- `native` is non-owning. With a null device, the executor owns CPU stubs
  and `native` is the stub address. With `SetDevice` before `Allocate()`,
  `native` is `nvrhi::ITexture*` / `IBuffer*` and the executor owns the
  NVRHI handles. `SetDevice` after `Allocate()` is `IncompatibleAccess`.
  Versions of one resource do not mint new natives. An aliased resource's
  registry entry points at the owner's native.
- `RegisterImport` still refuses `Create*` slots. After `Allocate()` without
  `RegisterImport`, `GetTexture` of an imported handle is
  `UnregisteredImport`.
- `GetAllocationStats()` reports physical counts and `estimatedBytes` for
  objects actually created (`width * height * BytesPerPixel`, or
  `bytesPerElement * numElements`). `logicalTextureCount`,
  `logicalBufferCount`, and `peakLogicalBytes` also count aliased `Create*`
  resources. `peakPhysicalBytes` equals `estimatedBytes`: objects are held
  until the executor is destroyed, so that total is the frame high-water
  mark. `savedBytes` is `peakLogicalBytes - peakPhysicalBytes`. `reusePairs`
  lists `(owner, alias)` in alias-index order; the owner is the resource
  that created the physical object. Stats are zero before `Allocate()`.
  PIX name agreement is a local check, not CI. An alias registry entry keeps
  the logical resource's `debugName`; the NVRHI object's debug name is the
  owner's.
- `GetExported` returns the registered or allocated token for the pinned
  exported version. It does not extract into a new object.
- The constructor copies compile success, cull states, live pass order, and
  resource lifetimes.
  `Execute(graphicsCommandList)` walks that live order. `SelectCommandList`
  currently returns the graphics list for every pass; a later `PassFlags`
  value can choose another queue there without changing the loop. Each live
  pass activates one executor-owned `PassContext`, calls the lambda stored
  by `GraphBuilder::AddPass`, then deactivates the context. There is no command
  list on `PassContext`. Culled passes are not called. A live pass with no
  lambda is `InvalidPass` and stops later passes.
- `GetTexture` / `GetBuffer` resolve only the declaring pass's read/write
  list, including the exact declared version (read version or write output
  version). Undeclared, stale, null, foreign-graph, type-mismatched, expired,
  and unbound lookups return `nullptr` and append an `rdg::Error` in every
  configuration. The callback does not receive the registry, the compile
  result, or renderer tables.
- S5.4 and S5.5 factories remain for CPU tests. Production frames use the
  S5.6 graph below and do not call them.
- S5.4 one-pass leaf: `BuildToneMapGraph` imports `HDRSceneColor`
  (`RGBA16Float`) and either `BackBuffer` (imported+exported Present) or
  `PostProcessColor` (imported-only dump target). One live Raster
  `PostProcess` pass. The app (`RenderLab` links `RenderLab::Rdg`)
  rebuilds GraphBuilder + Compile + GraphExecutor on every tone-map run,
  `RegisterImport`s, `Plan()`s, then `ExecutePass`. No `Allocate` (no
  `Create*`). `--manual-tonemap` restores the pre-S5.4 `Execute` call for
  `PresentSource::Final` and `--output-hdr` `final.png`. `--dump-rdg` stays
  the M1-shaped 3-pass graph; the one-pass dump is asserted in
  `tests/test_rdg_tonemap.cpp`.
- S5.5 lighting-to-present: `BuildLightingPresentGraph` imports GBufferA/B/C
  and GBufferDepth, Create*s `HDRSceneColor` (`RGBA16Float`), and imports
  either `BackBuffer` (imported+exported Present) or `PostProcessColor`
  (imported-only dump target). Two live Raster passes (`DeferredLighting`,
  `PostProcess`) plus a declare-only `LightingDebug` branch that culls as
  `unused-leaf`. The app rebuilds GraphBuilder + Compile + GraphExecutor on
  every Final run, `RegisterImport`s GBuffer/depth/output, `SetDevice`s,
  `Allocate`s HDR, `Plan()`s, then two `ExecutePass` callbacks on one
  captured command list (`GetTexture` of the write-minted HDR handle). After
  lighting, `copyTexture` mirrors RDG HDR into app-owned `HDRSceneColorTarget`
  so `DumpHdrCapture` is unchanged. FormatMap is not flipped (RGBA16Float
  create-time clear stays `(0,0,0,0)`; lighting `Execute` still clears
  `(0,0,0,1)`). `--manual-tonemap` restores manual lighting into app HDR plus
  `PostProcessPass::Execute` for on-screen Final; `final.png` still uses
  `ExecuteToneMap`. LightingDebug / GBufferDebug stay fully manual including
  lighting. `--dump-rdg` stays the M1-shaped 3-pass graph; the runtime graph
  dump is asserted in `tests/test_rdg_lighting.cpp`.
- S5.6 raster frame: `BuildRasterFrameGraph` declares `RasterPresent::Final`
  (GBuffer, DeferredLighting, PostProcess), `LightingDebug` (GBuffer,
  DeferredLighting, LightingDebug), and `GBufferDebug` (GBuffer,
  GBufferDebug). GBuffer and depth are Create*. HDR is Create* only when
  the present pass reads it. GBufferDebug omits the HDR chain so compile
  does not report `ZeroUseAllocation`. BackBuffer stays imported and
  exported; GBuffer and HDR are not exported. The app mirrors those
  declarations with `GraphBuilder::AddPass` lambdas that call the existing
  pass `Execute` functions, then `Compile`, `SetDevice`, `RegisterImport`
  of the back buffer, `Allocate`, `Plan`, and one `Execute`. RDG errors are
  logged and the frame does not fall back to a manual schedule. The
  executor and graph stay members until the next frame or
  `BackBufferResizing` (pass framebuffers released first, then the
  executor, then device idle / GC). Zero-size frames do not `Allocate`.
  Dumps and the HUD read a non-owning snapshot filled during the lambdas.
  A GBuffer-debug frame's HDR pointer is null: `--output-hdr` with
  `--gbuffer-view` fails and does not re-run DeferredLighting. `final.png`
  calls `PostProcessPass::Execute` directly. The executor is still rebuilt
  every frame, so pass framebuffer caches miss across frames. `--dump-rdg`
  stays the M1-shaped 3-pass golden. Shape, access, allocate, and execute
  order are asserted in `tests/test_rdg_raster.cpp`.
- S5.7 transient reuse: `PlanTransientReuse` runs inside `Allocate()`.
  Eligible resources are non-imported, non-exported `Create*` with a mapped
  descriptor. The key is texture `format + width + height`, or buffer
  `bytesPerElement + numElements`. Debug names are not part of the key.
  Overlap uses the closed live-slot interval of `ResourceLifetime`: two
  resources alias only when `lastSlot < firstSlot` in one direction. Pass
  indices are not compared as numbers, because `firstPass` is the pass at
  the earliest slot and can be numerically larger than `lastPass`.
  Candidates are ordered by first slot, then resource index. A candidate
  reuses the free physical slot with the lowest owner index whose occupied
  last slot ends before the candidate starts, then extends that slot.
  `SetTransientReuse(false)` (default true; CLI `--no-transient-reuse`)
  gives every `Create*` its own object. A call after `Allocate()` is
  `IncompatibleAccess`. Unknown formats stay `InvalidDescriptor` and are
  not pooled. If the owner's create fails, its aliases record
  `AllocationFailed` and are not created separately. Reuse does not issue a
  second clear; the alias's first live access is a write. The Final,
  LightingDebug, and GBufferDebug production graphs have no reuse pair:
  `GBufferB` and `HDRSceneColor` are both `RGBA16Float` at the viewport size
  and both live on `DeferredLighting`. With reuse left on, those graphs
  still report Final/M1 `textureCount == 5` and `1280 * 720 * 28` bytes, and
  GBufferDebug `textureCount == 4` and `1280 * 720 * 20` bytes. Synthetic
  interval cases are in `tests/test_rdg_reuse.cpp`.
