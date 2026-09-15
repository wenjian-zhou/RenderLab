# RDG Model and Boundary Contract

Status: **active design note — Stage 5** (implemented through S5.1)

Step: S5.1

This file records the mini RDG's logical model: handles, resource
descriptors, pass records, read/write declarations, version provenance,
dependency edges, topological order, output-driven pass culling, logical
resource lifetimes, and compiled-graph text/DOT dumps. S5.1 adds the
NVRHI-free execution layer that binds imported handles to opaque physical
tokens. How this design relates to UE 5.8.1's render
dependency graph — what is adopted,
diverged, and skipped, with `file:line` citations — lives in
[`ue-rdg-survey.md`](ue-rdg-survey.md) and
[`adr/ADR-003-rdg-boundary.md`](adr/ADR-003-rdg-boundary.md); this file does
not repeat those arguments. Renderer terminology follows
[`renderer-conventions.md`](renderer-conventions.md). A drawn walkthrough of
the S4.2–S4.6 code — version history, the declaration flow, the per-method
field-access matrix, compilation, culling, lifetimes, and compiled dumps — is in
[`rdg-dataflow.md`](rdg-dataflow.md).

## 1. Scope and boundary

The `src/rdg` module (`RenderLabRdg` target, namespace `renderlab::rdg`) is
the **pure logical CPU model** of the render dependency graph:

- It owns logical resource identity, descriptors, pass records, explicit
  read/write declarations, logical versions with producer/reader provenance,
  imported/exported flags, pass flags, declaration-time validation,
  RAW/WAR/WAW dependency edges, a deterministic pass order or cycle
  diagnostic, output-driven pass culling with stable reasons, logical
  first/last-use lifetimes after culling, and stable text/DOT dumps of the
  compiled graph.
- It must not include or link Donut/NVRHI. `RenderLabRdg` links only
  `RenderLab::ProjectOptions`; the link closure is the proof, and review
  keeps it that way (ADR-003).
- Tests construct graphs with **no GPU device**; `RenderLabDataContractTests`
  runs entirely on CPU, including the CI runners.
- Physical import bind and pass execution context live in a separate
  target, `RenderLabRdgExec` (`src/rdg/exec`, namespace `renderlab::rdg::exec`).
  That target links only `RenderLab::Rdg` in S5.1. Internal allocation and
  format mapping are S5.2; access-state planning is S5.3. S5.7 reuses
  non-overlapping logical intervals; this file only defines the intervals.

The renderer is untouched by Stage 4; integration begins at S5.4 against the
frozen M1 reference ([`m1-reference.md`](m1-reference.md)).

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

Both are value types with `operator==` — the future pooling key for S5.7
(UE precedent: `FRDGBufferDesc::operator==`).

`Format` is a **neutral owned enum** (`SRGBA8Unorm`, `RGBA8Unorm`,
`RGBA16Float`, `RGBA32Float`, `R32Float`, `D32Float`), covering the frozen
M1 pipeline resources. S5.2 owns the mapping table from these values to
`nvrhi::FormatType`; Stage 4 never names an RHI type. Extend the enum only
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

- `PassFlags` starts as `None | Raster | NeverCull`. `Raster` is
  informational until S5.3 access planning; S4.6 dumps print it.
  `NeverCull` is the side-effect flag (UE precedent — survey §4): the pass
  and its last-producer closure survive culling. Unknown bits are rejected.
  `Compute`/`Copy`/`Readback` are added when a real pass needs them.
- Accepted reads pin the input version; accepted writes pin the new output
  version. Same-resource conflicts follow the S4.2 rules above.
- Pass and resource names must be non-empty; duplicates are allowed.
- There is no execute lambda on `AddPass`. S4.3 compiles declarations into a
  pass order; S4.4 culls passes that cannot affect an exported resource or
  declared side effect; S5.1's `GraphExecutor::ExecutePass` is a synthetic
  driver that never records GPU commands.

## 5. Failure taxonomy

Every mutating call (`Create*`, `Import*`, `AddPass`, `Read`, `Write`,
`Export*`) validates immediately. A rejected call records one or more
structured errors and leaves the graph unchanged by that call. Execution-time
lookup (`RegisterImport`, `GetExported`, `GetTexture` / `GetBuffer`) uses the
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
| `IncompatibleAccess` | same-pass read/write combination, Write after Export, RegisterImport of a non-imported or already-bound resource, or GetExported of a non-exported resource |
| `TypeMismatch` | handle kind does not match the registry slot's kind (only reachable with a forged handle) |
| `InvalidName` | empty pass or descriptor name |
| `InvalidDescriptor` | zero extent, unknown texture format, or zero element size/count |
| `InvalidPassFlags` | pass flags outside the known bit set |
| `ZeroUseAllocation` | compile-time: a non-imported resource has no live-pass access after culling |
| `UndeclaredAccess` | `GetTexture` / `GetBuffer` names a resource that exists on the graph but is not in this pass's read/write list |
| `ExpiredContext` | `Get*` after `ExecutePass` has returned |
| `UnregisteredImport` | declared or exported handle has no physical token (S5.1 internals stay unbound) |
| `InvalidPass` | `ExecutePass` on a culled, out-of-range, or failed-compile pass |

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
declared `GetTexture` / `GetBuffer` during `ExecutePass`, undeclared /
stale / null / foreign / type-mismatch / expired lookup, unbound internals,
culled and out-of-range `ExecutePass`, imported buffers, and the M1-shaped
BackBuffer rule (only PostProcess may resolve it). No GPU device.

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

## 10. Execution layer (S5.1)

`GraphBuilder::ImportTexture({desc})` stays logical-only. After compile,
`renderlab::rdg::exec::GraphExecutor` binds already-existing objects:

```text
PhysicalTexture / PhysicalBuffer { void* native; std::string debugName; }
GraphExecutor::RegisterImport(handle, physical)
GraphExecutor::GetExported(handle)
GraphExecutor::ExecutePass(passIndex, callback)
PassContext::GetTexture / GetBuffer
```

- `native` is non-owning. Tests pass the address of a stack dummy. S5.2 will
  store device pointers as `void*` in the same slot. Physical identity is per
  resource index; `RegisterImport` accepts any in-range version of an
  imported resource.
- `GetExported` returns that same token once the import is registered. It
  does not extract into a new object. The handle must be the pinned exported
  (current) version.
- `ExecutePass` copies compile success and cull state in the constructor,
  requires a live pass, activates one executor-owned `PassContext` for the
  callback, then deactivates it. There is no command list.
- `GetTexture` / `GetBuffer` resolve only the declaring pass's read/write
  list, including the exact declared version (read version or write output
  version). Undeclared, stale, null, foreign-graph, type-mismatched, expired,
  and unbound lookups return `nullptr` and append an `rdg::Error` in every
  configuration. The callback does not receive the registry, the compile
  result, or renderer tables.
- `RegisterImport` refuses `CreateTexture` / `CreateBuffer` slots. Internal
  physical allocation is S5.2.
