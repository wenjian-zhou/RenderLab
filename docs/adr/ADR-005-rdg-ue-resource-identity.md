# ADR-005: One RDG Resource Identity, and Barriers in Execute

- Status: Accepted
- Date: 2026-09-26
- Supersedes: the versioned-handle and explicit `Read` / `Write` bullets in
  [ADR-003](ADR-003-rdg-boundary.md) and
  [ADR-004](ADR-004-rdg-single-module.md)
- Related: [`../rdg.md`](../rdg.md),
  [`../../IMPLEMENTATION_PLAN.md`](../../IMPLEMENTATION_PLAN.md)

## Decision

A texture or buffer has one handle for the whole graph: `{index, graphId}`.
A write does not mint a new identity. Passes declare
`Use(handle, Access)`. `ShaderResource`, `RenderTarget`, and `DepthWrite`
are the pass uses. `Present` is only an initial or final boundary.
`SetInitialAccess` records an import's starting access. `ExportTexture` /
`ExportBuffer` record the access the resource must have when `Execute`
returns. `Create*` resources take their initial access from the same rule
`FormatMap` uses for `initialState` (color `RenderTarget`, `D32Float`
`DepthWrite`, buffer `ShaderResource`).

Pass order is `AddPass` order. There is no topological sort and no cycle
diagnostic. An edge runs from a resource's last earlier writer to a later
reader. Two writers do not get an edge. A reader does not get an edge to a
later writer. A non-import `ShaderResource` with no earlier writable use is
`ReadBeforeProduce`.

`GraphExecutor::Execute` issues the barriers. When `Plan()` succeeded, each
live pass is preceded by a transition from the tracked state to the
declared access, then one `commitBarriers`. After the last live pass, only
exported resources are transitioned to their final access. Non-exported
restore rows stay in the plan and are not issued. `FormatMap` sets
`keepInitialState = false` on textures and buffers it creates, and
`Execute` calls `beginTrackingTextureState` / `beginTrackingBufferState`
from the planned initial access. Swapchain and other renderer resources
that already set `keepInitialState = true` are unchanged. `Execute(nullptr)`
records the same transition log and does not call NVRHI.

A later command list that samples a `Create*` texture must
`beginTrackingTextureState` from the access that texture still has. After
a Final frame that is `ShaderResource` for the GBuffer targets and for
HDR. The back buffer is exported as `Present`.

## What this does not change

- Collected `rdg::Error` values.
- Public `Compile()`.
- Cull reasons and output-driven culling. Roots are still `NeverCull`, or
  a writable use of an imported or exported resource.
- Text and DOT dumps, including `--dump-rdg`. The grammar no longer has
  `versions:`, `cycle:`, or `vN`.
- S5.7 reuse: format and size, non-overlapping live slots, import and
  export ineligible, `--no-transient-reuse`.
- Pass `Execute` still clears and calls `setGraphicsState`. The executor
  does not own the framebuffer.

## Context

UE 5.8.1 keeps one `FRDGTexture*` for the whole graph. A write does not
allocate a new producer identity, and pass order is the order passes were
added. RenderLab's versioned handles, `Read` / `Write`, and RAW/WAR/WAW
topological order were the Stage 4 model. They made a later write look like
a different resource, and they rejected graphs that UE schedules by
insertion. `keepInitialState = true` hid the missing barriers by restoring
the create-time state when the command list closed. With one identity, the
graph has to say the access, and `Execute` has to issue it.
