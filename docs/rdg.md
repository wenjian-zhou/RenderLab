# RDG Model and Boundary Contract

Status: **active design note — Stage 4** (implemented through S4.2)

Step: S4.2

This file records the mini RDG's logical model: handles, resource
descriptors, pass records, read/write declarations, and the failure
taxonomy. How this design relates to UE 5.8.1's render dependency graph —
what is adopted, diverged, and skipped, with `file:line` citations — lives in
[`ue-rdg-survey.md`](ue-rdg-survey.md) and
[`adr/ADR-003-rdg-boundary.md`](adr/ADR-003-rdg-boundary.md); this file does
not repeat those arguments. Renderer terminology follows
[`renderer-conventions.md`](renderer-conventions.md). A drawn walkthrough of
the S4.2 code — version history, the declaration flow, and the per-method
field-access matrix — is in [`rdg-dataflow.md`](rdg-dataflow.md).

## 1. Scope and boundary

The `src/rdg` module (`RenderLabRdg` target, namespace `renderlab::rdg`) is
the **pure logical CPU model** of the render dependency graph:

- It owns logical resource identity, descriptors, pass records, explicit
  read/write declarations, logical versions with producer/reader provenance,
  imported/exported flags, pass flags, and
  declaration-time validation.
- It must not include or link Donut/NVRHI. `RenderLabRdg` links only
  `RenderLab::ProjectOptions`; the link closure is the proof, and review
  keeps it that way (ADR-003).
- Tests construct graphs with **no GPU device**; `RenderLabDataContractTests`
  runs entirely on CPU, including the CI runners.
- What is deliberately absent and who owns it: dependency edges and
  topological order (S4.3), culling (S4.4),
  lifetimes (S4.5), full graph text/DOT dumps and compile error categories (S4.6),
  physical resources and access states (Stage 5).

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

`Format` is a **neutral owned enum** (`Srgba8Unorm`, `Rgba8Unorm`,
`Rgba16Float`, `Rgba32Float`, `R32Float`, `D32Float`), covering the frozen
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

`imported || exported` marks a cull root; S4.4 consumes that rule. An imported
resource may also be written (back-buffer precedent).

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
a version already produced by a newer pass. Cycle detection and actual
execution ordering belong to S4.3; S4.2 does not claim the graph is schedulable.

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
  informational in S4.1 (S5.3 access planning and S4.6 dumps consume it);
  `NeverCull` is the side-effect flag (UE precedent — survey §4). Unknown
  bits are rejected. `Compute`/`Copy`/`Readback` are added when a real pass
  needs them.
- Accepted reads pin the input version; accepted writes pin the new output
  version. Same-resource conflicts follow the S4.2 rules above.
- Pass and resource names must be non-empty; duplicates are allowed.
- There is no execute lambda and no compile/finish phase in Stage 4;
  execution is Stage 5.

## 5. Failure taxonomy

Every mutating call (`Create*`, `Import*`, `AddPass`, `Read`, `Write`,
`Export*`) validates immediately. A rejected call records one or more
structured errors and leaves the graph unchanged by that call. Errors are
**collected, never thrown**, and never implicitly asserted — the explicit
`GraphBuilder::AssertNoErrors()` is the Debug trap for non-test callers.
S4.6 builds the compile error categories on this structure.

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
| `IncompatibleAccess` | same-pass read/write combination, or Write after Export |
| `TypeMismatch` | handle kind does not match the registry slot's kind (only reachable with a forged handle) |
| `InvalidName` | empty pass or descriptor name |
| `InvalidDescriptor` | zero extent, unknown texture format, or zero element size/count |
| `InvalidPassFlags` | pass flags outside the known bit set |

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

## 8. Version dump

`GraphBuilder::DumpVersions()` returns deterministic text in resource-index,
then version order. Each row names the resource kind/index/name, version,
producer (pass index/name, `external`, or `unproduced`), reader indices/names,
and current/exported status. It omits graph IDs so identical declarations
produce identical text across builders. This is S4.2 provenance evidence;
S4.6 adds full compiled-graph diagnostics and DOT output.

```text
texture 0 'Input' v0 producer=external readers=[0 'First'] current
texture 1 'Intermediate' v0 producer=unproduced readers=[]
texture 1 'Intermediate' v1 producer=0 'First' readers=[1 'Second'] current
texture 2 'Output' v0 producer=unproduced readers=[]
texture 2 'Output' v1 producer=1 'Second' readers=[2 'Third'] current exported
```
