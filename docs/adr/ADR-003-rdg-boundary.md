# ADR-003: RDG Boundary — Stage 4 Is a Pure Logical CPU Model

- Status: Accepted
- Date: 2026-09-08
- Step: S4.1
- Related: [`../rdg.md`](../rdg.md), [`../ue-rdg-survey.md`](../ue-rdg-survey.md),
  [`../m1-reference.md`](../m1-reference.md),
  [`../../IMPLEMENTATION_PLAN.md`](../../IMPLEMENTATION_PLAN.md) §2.3, §8

## Decision

The Stage 4 mini-RDG compiler is a **pure logical CPU model** in its own
module (`src/rdg`, target `RenderLabRdg`, namespace `renderlab::rdg`) with
zero Donut/NVRHI dependencies — `RenderLabRdg` links only
`RenderLab::ProjectOptions`. Three load-bearing choices define the boundary:

1. **Typed value handles are the public resource identity.** A handle is
   `{index, version, graphId}` — three flat `uint32`s, no bit packing — and
   the handle **is** the version identity: S4.2's writes mint new handles.
2. **Read/write are explicit declarations on pass records**, not bits
   folded into an access enum.
3. **Errors are collected structured values** (category + pass name +
   resource name), never fatal `checkf`-style aborts.

Stage 5 is the execution layer that maps this model onto NVRHI; until S5.4
the renderer is untouched (M1 freeze).

## Context

- `IMPLEMENTATION_PLAN.md` §8 makes Stage 4 GPU-independent by design, and
  §2.3 assigns RenderLab ownership of "logical RDG resources, versions,
  dependencies, culling, lifetimes, and state intent."
- The GitHub CI runners have no GPU, so graph semantics must be testable
  without a device.
- UE 5.8.1's RDG is monolithic: one component owns the logical graph,
  physical state, allocation, barriers, and execution (survey §1). The
  survey's §10 table records, per area, what RenderLab adopts, diverges,
  and skips; this ADR is the decision record for the boundary itself.
- RDG is a primary learning deliverable, so no third-party frame-graph
  library may replace it (plan §2.3).

## Options Considered

### 1. Adopt UE's monolithic builder shape

Logical + physical + barriers + execution in one component, integrated with
the renderer from the start.

Rejected. The plan deliberately splits a CPU compiler (Stage 4) from the
execution layer (Stage 5): graph semantics — versioning, ordering, culling,
lifetimes — become testable in GPU-less CI, and the frozen M1 renderer
stays untouched until parity can be proven against its goldens.

### 2. UE-style public identity: raw pointers + debug validation

UE passes `FRDGTexture*` around and catches dangling use via debug
validation; internal handles are index-only with no generation (survey §2).

Diverged — ours is stronger. Our handles are the public API, so
version-in-handle plus a per-builder `graphId` makes stale, null,
cross-graph, and type-mismatch failures deterministic in every build, not
just under a debug layer. Adopted from UE: the null-sentinel pattern,
index ordering for future first/last-pass bookkeeping, and per-type handle
classes (as two concrete structs rather than a template, matching repo
taste).

### 3. No per-write versioning (UE's model)

UE has no `Version` anywhere in its RDG; WAW ordering comes from insertion
order plus last-producer edges plus per-subresource state (survey §3).

Rejected for S4.2. The plan wants every write to create an explicit new
logical version, the classic FrameGraph model: it buys deterministic
superseded-use diagnostics and cycle/read-before-produce detection that
UE's design cannot express (survey §10 "Logical versioning" row).

### 4. Version in a side table keyed by handle

Staleness would then be a validation lookup, not a property of the value
(survey §11 Q5).

Rejected. S4.2's "a write returns a new handle" depends on the handle being
the version identity; a side table weakens that to a validation-time
convention and re-introduces the exact staleness ambiguity the version
field exists to remove.

### 5. Read/write encoded in an access enum (UE's `ERHIAccess`)

UE derives read-vs-write from `WritableMask` on the access value (survey
§4).

Deferred to S5.3. The logical compiler layer has no RHI, so an explicit
per-declaration read/write bit is the honest model; the access enum and
its mask *shape* become the S5.3 access-state subset when NVRHI states are
planned.

### 6. Format representation in descriptors

RHI-native formats (UE's `FRDGTextureDesc : FRHITextureDesc`) would drag
NVRHI headers into the module; deferring format entirely would leave
descriptors unable to describe the M1 resources.

Selected: a small neutral owned enum covering the frozen M1 formats
([`../rdg.md`](../rdg.md) §3). S5.2 owns the mapping table to
`nvrhi::FormatType`.

### 7. UE's fatal validation style

`checkf` for lifecycle/provenance, `ensureMsgf` for read-before-write
(survey §9).

Diverged. Errors are collected with a category and the involved pass and
resource names — the shape S4.6's compile error categories need — so a
rejected declaration is data a test can assert on. The Debug trap is
explicit (`assertNoErrors()`), not implicit in every call.

### 8. Skipped wholesale

Per survey §0/§10: async compute and multi-queue scheduling, parallel
setup/compile/execute, render-pass merging, uniform buffers and the
parameter-struct macro machinery, the blackboard, the RHI transient
allocator, RDG Insights, subresource-granular state tracking, and memory
budgeting.

## Consequences

- The zero-NVRHI property is structural: `RenderLabRdg` compiles from five
  files against only `ProjectOptions`; any future NVRHI reference is a
  boundary violation caught in review.
- Handles are 12-byte forgeable values by design; the failure matrix tests
  forged handles, and the registry validation (unified registry with a
  runtime kind tag — itself a divergence from UE's per-type registries) is
  the safety net.
- `graphId` from a global counter allows multiple concurrent builders; no
  single-active-builder restriction (UE has one).
- S5.2 must map `Format` → `nvrhi::FormatType` and descriptors → NVRHI
  descriptors; S5.7 uses descriptor equality as the pooling key; S4.4
  consumes `imported || exported` as the cull-root rule.
- In S4.1, `StaleVersion` and `TypeMismatch` are reachable only through
  forged handles (no production write bumps a version yet, and honest code
  is caught at compile time) — an accepted, documented limitation until
  S4.2.

## Validation Performed In S4.1

- Debug and Release builds clean (0 errors, 0 warnings);
  `RenderLabRdg` builds with no NVRHI/Donut dependency.
- `RenderLabDataContractTests` Debug and Release: 0 failures, with 87 new
  RDG checks — the four-category failure matrix (null, cross-graph, stale
  version incl. out-of-range, type mismatch in both directions) at every
  entry point, deterministic validation order, creation validation,
  verbatim declaration recording, import/export semantics, and an
  M1-shaped graph (six resources, three passes, imported+exported back
  buffer) constructed with no GPU device.
- M1 regression insurance unchanged: `smoke.ps1` errors=0;
  `golden.ps1` and `golden-hdr.ps1` `-Mode Verify -Configuration Debug`
  pass, with the channel-swap negative checks failing as expected.
