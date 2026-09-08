# RDG Model and Boundary Contract

Status: **active design note — Stage 4** (model as built in S4.1; extended by
S4.2–S4.6)

Step: S4.1

This file records the mini RDG's logical model: handles, resource
descriptors, pass records, read/write declarations, and the failure
taxonomy. How this design relates to UE 5.8.1's render dependency graph —
what is adopted, diverged, and skipped, with `file:line` citations — lives in
[`ue-rdg-survey.md`](ue-rdg-survey.md) and
[`adr/ADR-003-rdg-boundary.md`](adr/ADR-003-rdg-boundary.md); this file does
not repeat those arguments. Renderer terminology follows
[`renderer-conventions.md`](renderer-conventions.md).

## 1. Scope and boundary

The `src/rdg` module (`RenderLabRdg` target, namespace `renderlab::rdg`) is
the **pure logical CPU model** of the render dependency graph:

- It owns logical resource identity, descriptors, pass records, explicit
  read/write declarations, imported/exported flags, pass flags, and
  declaration-time validation.
- It must not include or link Donut/NVRHI. `RenderLabRdg` links only
  `RenderLab::ProjectOptions`; the link closure is the proof, and review
  keeps it that way (ADR-003).
- Tests construct graphs with **no GPU device**; `RenderLabDataContractTests`
  runs entirely on CPU, including the CI runners.
- What is deliberately absent and who owns it: versioning semantics
  (S4.2), dependency edges and topological order (S4.3), culling (S4.4),
  lifetimes (S4.5), text/DOT dumps and compile error categories (S4.6),
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
- `version` — the S4.2 logical-version slot. S4.1 mints every handle at 0;
  the plan's "a write returns a new handle" (S4.2) will bump it, because the
  handle **is** the version identity.
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
| `createTexture` / `createBuffer` | — | graph-internal logical allocation |
| `importTexture` / `importBuffer` | `Imported` | physical counterpart exists outside the graph (UE `bExternal`); in S4.1 the descriptor describes it for S5.1 mapping |
| `exportTexture` / `exportBuffer` | `Exported` | output leaves the graph (UE `bExtracted`); sets the flag only |

`imported || exported` marks a cull root; S4.4 consumes that rule. Exporting
is idempotent, and an imported resource may also be written (back-buffer
precedent); the full legality semantics arrive with S4.2 produce tracking.

## 4. Passes and declarations

`GraphBuilder::addPass(name, flags)` appends a `PassRecord` and returns a
`PassBuilder` — a value holding the builder and the pass index (never a
pointer into storage that can reallocate). Declarations hang off the pass:

```cpp
auto pass = builder.addPass("DeferredLighting", PassFlags::Raster);
pass.read(gbufferA);
pass.write(hdrSceneColor);
```

- `PassFlags` starts as `None | Raster | NeverCull`. `Raster` is
  informational in S4.1 (S5.3 access planning and S4.6 dumps consume it);
  `NeverCull` is the side-effect flag (UE precedent — survey §4). Unknown
  bits are rejected. `Compute`/`Copy`/`Readback` are added when a real pass
  needs them.
- Declarations are recorded **verbatim**: no dedup, no same-pass conflict
  rules — those are S4.2/S4.3 semantics. Read and write of the same
  resource in one pass record two accesses.
- Pass and resource names must be non-empty; duplicates are allowed.
- There is no execute lambda and no compile/finish phase in Stage 4;
  execution is Stage 5.

## 5. Failure taxonomy

Every mutating call (`create*`, `import*`, `addPass`, `read`, `write`,
`export*`) validates immediately. A rejected call records one or more
structured errors and leaves the graph unchanged by that call. Errors are
**collected, never thrown**, and never implicitly asserted — the explicit
`GraphBuilder::assertNoErrors()` is the Debug trap for non-test callers.
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
| `StaleVersion` | version does not match the registry slot's current version, or the index is out of range on the owning graph |
| `TypeMismatch` | handle kind does not match the registry slot's kind (only reachable with a forged handle) |
| `InvalidName` | empty pass or descriptor name |
| `InvalidDescriptor` | zero extent, unknown texture format, or zero element size/count |
| `InvalidPassFlags` | pass flags outside the known bit set |

Validation order is deterministic — when a forged handle violates several
rules at once, the earliest category wins:

```text
null → owning graph → index range → kind → version
```

Note on S4.1 reachability: no production path bumps a version yet (writes
minting new handles is S4.2), and compile-time handle types cover honest
use. `StaleVersion` and `TypeMismatch` therefore surface only through
forged handles — which is exactly what the failure-matrix tests construct.

## 6. Usage conventions

- `GraphBuilder` is stack-scoped and non-copyable; pass `PassBuilder`
  values around freely, but they are valid only while their builder lives.
- A `PassBuilder` returned from a rejected `addPass` is invalid
  (`isValid() == false`); its `read`/`write` are no-ops, so a partially
  rejected graph remains explorable.
- Declarations may be interleaved across passes; each `PassBuilder` keeps
  targeting its own pass record.
- Single-threaded declaration recording, like the rest of the renderer's
  frame loop; the global `graphId` counter is the only shared state.

## 7. Verification

`tests/test_rdg_handles.cpp` (34 checks) covers graph identity, minted
handle fields, the four-category failure matrix at every entry point
(read/write/export), validation order, and verbatim recording.
`tests/test_rdg_declarations.cpp` (53 checks) covers descriptor validation,
import/export flag semantics, descriptor equality, pass flag validation,
per-declaration rejection isolation, and an M1-shaped graph — six
resources and three passes mirroring
[`m1-reference.md`](m1-reference.md) §3 — constructed with no GPU device.
