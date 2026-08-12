# Stage 2: D3D12 Core and Three-Frame Lifetime Model

## Purpose

Stage 2 establishes the smallest observable D3D12 runtime that can present continuously while
three frames reuse GPU-owned objects only after the Direct queue has finished with them. Work is
split into seven closed loops. Each loop must build, run, and produce evidence before the next
dependent loop begins.

The repository remains Windows 11, x64, C++20, D3D12, and Win32 only. The dependency versions,
vcpkg baseline, `x64-windows-static-md` triplet, Microsoft package lock, and existing Stage 1
automation are inputs to this stage, not work items to revise.

## Stage-Wide Boundaries

### Included

- One selected hardware adapter, one D3D12 device, and one Direct command queue.
- One Win32 flip-model swap chain with exactly three back buffers.
- Three fixed frame contexts, one Direct command allocator per context, and a minimal reusable
  Direct command-list arrangement.
- Fixed CPU-visible RTV storage for swap-chain buffers and a basic CPU-visible DSV heap boundary.
- A Direct-queue fence, frame-fence ownership, queue-idle synchronization, resize, and shutdown.
- Debug Layer, DXGI debug factory, InfoQueue policy, DRED configuration, object names, and
  actionable HRESULT/device-removal reports.
- Basic generation handles, fence retirement, and deferred COM-object deletion with CPU tests.

### Explicitly excluded

- Render Graph code or pass/resource declarations.
- Raster pipelines, depth rendering, shaders, DXC integration beyond the already staged runtime,
  scene loading, textures, materials, cameras, or ImGui.
- DXR devices or pipelines beyond reporting the adapter's DXR capability.
- General persistent/transient descriptor allocators, bindless heaps, upload/readback rings, or
  D3D12 Memory Allocator integration.
- Compute or Copy queues, cross-queue synchronization, multi-threaded recording, or a general
  command-list pool.
- Timing, benchmarking, PIX capture workflows, or performance conclusions.

The DSV heap in this stage is only a fixed CPU-visible heap and naming/lifetime boundary. Creating
a depth resource and using a depth attachment belong to the later raster stage. The command-list
model is intentionally fixed and single-threaded; it must not grow into the multi-queue or worker
pool planned for later stages.

## Common Validation Contract

Every loop must preserve these checks:

- Debug and Release builds succeed with the existing presets.
- `ctest --preset windows-debug --label-regex cpu` and the corresponding Release command pass
  whenever CPU-test code changes.
- GPU and Win32 smoke tests run on a D3D12-capable local machine or a fixed GPU/driver self-hosted
  runner. A normal GitHub-hosted runner is not evidence for GPU correctness.
- A smoke or validation failure returns a nonzero process exit code and identifies the operation,
  HRESULT, and relevant adapter/device state. Tests must not pass merely because the process
  closed.
- Debug validation uses the Debug Layer and DXGI debug factory. GPU-Based Validation is an
  explicit command-line option, is exercised separately, and is never used for performance data.
- No accepted run contains an unhandled D3D12 or DXGI Error/Corruption message. Every Warning is
  fixed or documented by message ID and reason; broad severity suppression is forbidden.
- DRED breadcrumbs and page-fault reporting are configured before device creation. If device
  removal occurs, the failure path attempts to emit DRED data before releasing the device.
- Every created D3D12 object receives a stable debug name.

Unless a loop defines a longer run, a "stable run" means at least 300 presented frames in Debug
and Release. The final stage soak test is longer and supersedes this minimum.

## Closed Loop 1: Debug Bootstrap, DXGI Factory, and Adapter Report

### Scope

- Request the D3D12 Debug Layer in Debug builds before any D3D12 device probe.
- Support an explicit GPU-Based Validation switch that is off by default.
- Configure DRED auto-breadcrumbs and page-fault reporting before any device probe.
- Create a DXGI factory, using `DXGI_CREATE_FACTORY_DEBUG` when debug validation is active.
- Enumerate adapters with `IDXGIFactory6::EnumAdapterByGpuPreference`, record hardware/software
  classification, and perform a non-retained `D3D12CreateDevice` capability probe.
- Select a hardware adapter deterministically and print a structured startup report containing at
  least description, vendor/device IDs, LUID, dedicated memory, software flag, driver version when
  available, selection reason, and D3D12-capable status.

### Not included

- Retaining the application device, creating queues, querying the final device feature matrix, or
  creating a swap chain.
- WARP fallback as an implicit success path. An explicit future diagnostic WARP mode may be added,
  but Stage 2 hardware validation must fail clearly when no suitable hardware adapter exists.

### Deliverables

- D3D12 diagnostics bootstrap and DXGI adapter-selection modules under `src/gfx/d3d12/`.
- A data-only adapter report separated from console/debug-output formatting.
- Command-line/configuration plumbing for normal debug validation and opt-in GPU-Based Validation.
- CPU tests for deterministic selection from synthetic adapter records, including software-only,
  unsupported, and tie-breaking cases.

### Tests

| Layer | Method |
|---|---|
| CPU | Exercise adapter-ranking and report formatting with synthetic records; no GPU is required. |
| GPU | Run the adapter-report mode on the verified GPU and confirm the selected LUID and driver data are stable across repeated runs. |
| Win32 | Launch through the existing hidden smoke harness, create/destroy the DXGI bootstrap, and exit without creating a swap chain. |

### Diagnostics acceptance

- **Debug Layer:** Debug startup reports `enabled`; absence of `ID3D12Debug` is a validation
  failure, while Release reports the requested/effective state without silently changing it.
- **InfoQueue:** no retained device exists yet, so device InfoQueue acceptance is deferred to Loop
  2; the report must say `not-created`, not claim successful validation.
- **DRED:** breadcrumbs and page-fault settings are applied before every capability probe and their
  requested/configured state is recorded (the DRED settings interface has no effective-state
  getter).

### Exit and failure handling

Exit when CPU tests pass, a hardware adapter is selected deterministically, and repeated local
smoke runs produce a complete report with no DXGI debug Error/Corruption. On enumeration or probe
failure, retain the per-adapter HRESULT in the report and return nonzero if no valid hardware
adapter remains. Do not choose a software adapter merely to make the smoke test pass.

### Dependencies

Depends only on completed Stage 1. Loops 2 and 3 consume its diagnostics state, factory, selected
adapter, and adapter report.

## Closed Loop 2: Device, Direct Queue, and Base Error Handling

### Scope

- Create and retain one D3D12 device from the selected adapter.
- Query and report the actual device feature level, Shader Model, DXR tier, resource-binding tier,
  and other small set of capabilities required by later stages. Unsupported future capabilities
  are report data, not Stage 2 failures, unless D3D12 Core itself is unavailable.
- Create one Direct command queue and give the device and queue stable debug names.
- Introduce checked HRESULT helpers that preserve the failed expression/operation, source context,
  HRESULT text, and `GetDeviceRemovedReason()` when applicable.
- Attach the device InfoQueue and establish the Error/Corruption break and collection policy.

### Not included

- Swap chain, command allocators/lists, Compute or Copy queues, DXR objects, shader compilation,
  or recovery by recreating a removed device.

### Deliverables

- Device/queue ownership module with explicit destruction order.
- Data-only device capability report appended to the startup report.
- Central D3D12 failure type/formatter and device-removal reporting entry point.
- CPU tests for HRESULT formatting and feature-report serialization using synthetic values.

### Tests

| Layer | Method |
|---|---|
| CPU | Verify HRESULT success/failure classification, stable diagnostic text, and capability-report formatting without a GPU. |
| GPU | Create/destroy the device and Direct queue repeatedly; verify the report comes from the retained device rather than the adapter name. |
| Win32 | Run the hidden lifecycle smoke with device/queue initialization before window teardown and verify nonzero exit on forced initialization failure. |

### Diagnostics acceptance

- **Debug Layer:** device creation occurs only after Loop 1 enables validation; the device and queue
  appear with names and emit no Error/Corruption during create/destroy.
- **InfoQueue:** the device queue is attached immediately after creation; Error/Corruption cause a
  debug break when a debugger is present and are always counted as run failures. Warnings remain
  visible and classified.
- **DRED:** the removal-report path queries DRED breadcrumbs and page-fault output when
  `GetDeviceRemovedReason()` fails. A normal create/destroy run records that no removal occurred.

### Exit and failure handling

Exit when the selected hardware adapter creates a named device and Direct queue in Debug and
Release and the complete feature report is emitted. Device or queue creation failure returns
nonzero with the adapter identity and HRESULT. Do not fall back to another adapter after selection
without reporting and testing an explicit selection policy.

### Dependencies

Depends on Loop 1. Loops 3, 4, 6, and 7 require the retained device; Loop 3 also requires the
factory and queue.

## Closed Loop 3: Win32 Swap Chain, RTV Storage, and Stable Present

### Scope

- Create a windowed flip-discard swap chain for the existing `HWND` and Direct queue.
- Use exactly three back buffers, one sample per pixel, and an explicitly documented SDR format.
- Disable the legacy Alt+Enter association and make VSync behavior explicit for smoke runs.
- Create a fixed CPU-visible RTV heap, acquire all three back buffers, create their RTVs, and name
  the resources.
- Create the basic fixed CPU-visible DSV heap boundary required by the Stage 2 plan, without a
  depth resource.
- Present continuously and track the current swap-chain back-buffer index.

### Not included

- Render targets other than swap-chain buffers, depth resources, raster pipelines, shaders,
  tearing policy beyond an explicitly checked capability, HDR, fullscreen, or a general descriptor
  allocator.

### Deliverables

- Swap-chain/back-buffer owner with fixed RTV descriptor arithmetic.
- Basic fixed DSV heap owner, unused by rendering in this stage.
- A D3D12 present smoke mode with a bounded frame count and machine-readable success/failure exit.

### Tests

| Layer | Method |
|---|---|
| CPU | Test descriptor-index/CPU-handle arithmetic and invalid buffer-count/index rejection as pure functions. |
| GPU | Present at least 300 frames in Debug and Release and verify the reported back-buffer indices remain in `[0, 2]`. |
| Win32 | Exercise show, message pump, bounded present, `WM_CLOSE`, and teardown; the smoke harness enforces a timeout and exit code. |

### Diagnostics acceptance

- **Debug Layer:** swap chain, back buffers, and descriptor heaps create/destroy with no live-object
  or invalid-descriptor Error/Corruption.
- **InfoQueue:** all collected Error/Corruption counts remain zero after the final Present and
  teardown; Warnings are fixed or documented by ID.
- **DRED:** a Present failure enters the common device-removal path and emits available DRED data;
  a successful run reports no device removal.

### Exit and failure handling

Exit after stable bounded Present in both configurations and clean teardown. Any `Present`,
`GetBuffer`, or RTV creation failure returns nonzero with the buffer index and HRESULT. Occlusion
or minimized-window status must be classified separately from device removal. Do not insert a
per-frame queue-idle wait to stabilize Present.

### Dependencies

Depends on Loops 1 and 2. Loop 4 binds frame contexts to the three swap-chain buffers. Loop 5 owns
their resize/rebuild path.

## Closed Loop 4: Three Frame Contexts and Fence-Gated Reuse

### Scope

- Add exactly three frame contexts, associated with the three swap-chain buffer slots.
- Give each context one Direct command allocator and its last submitted Direct-fence value.
- Add one Direct fence, monotonically increasing signal values, one reusable event, and explicit
  CPU waiting for a particular fence value.
- Reuse a frame context only after its recorded fence value has completed; only then reset its
  allocator.
- Reset/reuse a minimal Direct command list, record a back-buffer transition and clear, transition
  back to Present, execute, present, and signal the frame's fence value.
- Allow the CPU to submit ahead until it reaches a still-in-flight context. A per-frame global
  `WaitForGpu` is forbidden.

### Not included

- General command allocator/list pools, parallel recording, bundles, Compute/Copy fences, upload
  memory, descriptor retirement, or Render Graph barriers.

### Deliverables

- Fixed `FrameContext[3]` owner and Direct fence/event synchronization helper.
- A small pure CPU frame-reuse state model used by both tests and assertions in the runtime.
- A bounded three-frame render smoke mode that records clear commands and exposes submitted,
  completed, waited, and allocator-reset values in validation logs.

### Tests

| Layer | Method |
|---|---|
| CPU | Simulate completed-fence progress and prove that reset is rejected before completion, accepted at equality, and independent for all three contexts. |
| GPU | Render/present at least 1,000 frames; confirm three distinct contexts submit and that CPU waits occur only when the selected context is still in flight. |
| Win32 | Keep pumping messages during fence waits and close cleanly from any current frame index without deadlock. |

### Diagnostics acceptance

- **Debug Layer:** no `COMMAND_ALLOCATOR_SYNC`, command-list state, resource-state, or fence errors;
  all frame allocators, the list, fence, and event-owning component are named/identifiable.
- **InfoQueue:** Error/Corruption is zero for the full 1,000-frame run; no lifecycle Warning is
  suppressed to pass the test.
- **DRED:** submission, Present, Signal, and wait failures route through device-removal reporting;
  breadcrumbs are available if removal occurs during recorded clear work.

### Exit and failure handling

Exit when CPU tests cover all fence boundaries and the GPU run demonstrates true three-context
reuse without a global per-frame wait. A wait timeout is a hard failure with current/completed and
target fence values plus frame index. If instability appears, preserve a minimal reproduction and
repair fence ownership; do not ship a per-frame queue-idle workaround.

### Dependencies

Depends on Loops 2 and 3. Loop 5 uses its queue-idle primitive and frame state. Loop 7 uses its
Direct-fence retirement tokens.

## Closed Loop 5: Resize, Queue Idle, and Resource-Rebuild Boundary

### Scope

- Convert `WM_SIZE` into a deferred resize request handled outside the window callback.
- Treat zero-sized/minimized windows as suspended presentation without destroying the device.
- Define queue idle as signal-once then wait-for-that-value, used only for resize, shutdown, and
  explicit exceptional boundaries.
- Before `ResizeBuffers`, idle the Direct queue and release every reference to old back buffers.
- Recreate the swap-chain buffers and RTVs for the new nonzero client size, reset current indices,
  and invalidate only size-dependent state.
- Make shutdown ordering explicit: stop submission, idle if work was submitted, release swap-chain
  resources, then queue/device/diagnostics objects.

### Not included

- Render-resolution scaling, fullscreen transitions, HDR/color-space changes, depth recreation,
  temporal-history resources, or device-lost recovery.

### Deliverables

- Deferred resize state in the Win32/D3D12 boundary.
- One audited queue-idle implementation and swap-chain resource-rebuild function.
- A scripted resize smoke mode covering grow, shrink, minimize/restore, repeated same-size events,
  and close-during/after-resize.

### Tests

| Layer | Method |
|---|---|
| CPU | Test resize-request coalescing, zero-size suspension, unchanged-size no-op, and rebuild decision rules. |
| GPU | Alternate several nonzero sizes during a 1,000-frame clear/present run; confirm rendering resumes with correct dimensions and indices. |
| Win32 | Drive `WM_SIZE` sequences including minimize/restore and close; enforce timeout, successful exit, and no Present while the client size is zero. |

### Diagnostics acceptance

- **Debug Layer:** no live back-buffer reference at `ResizeBuffers`, invalid RTV, resource-state,
  allocator, or shutdown errors.
- **InfoQueue:** zero Error/Corruption across every resize sequence; any Warning is tied to its
  message ID and resolved or documented.
- **DRED:** resize/Present failures invoke device-removal reporting; a fence-wait timeout records
  DRED availability and all relevant fence/frame values before aborting.

### Exit and failure handling

Exit after all scripted sequences pass in Debug and Release and a repeated-resize run completes
without lifecycle messages. Failure preserves the requested/client/old buffer sizes and the exact
operation/HRESULT. Do not retry `ResizeBuffers` while stale references remain, and do not turn
queue idle into a normal frame operation.

### Dependencies

Depends on Loops 3 and 4. Loop 6 performs the final diagnostics audit against its resize and
shutdown paths.

## Closed Loop 6: Debug Layer, InfoQueue, and DRED Validation Audit

### Scope

- Consolidate debug configuration into one startup policy with requested, available, and effective
  states in the startup report.
- Drain/classify D3D12 InfoQueue and DXGI debug messages at validation checkpoints and shutdown.
- Fail validation on Error/Corruption and maintain an exact-ID allowlist only for reviewed,
  documented harmless Warnings. Start with an empty allowlist.
- Confirm DRED breadcrumbs and page-fault reporting are configured before device creation and make
  device-removal output robust when only part of the DRED data is available.
- Add an opt-in validation probe that verifies application-message collection and severity policy
  without intentionally corrupting GPU state.
- Perform a live-object report after normal teardown in Debug validation runs.

### Not included

- Automatic device recreation, crash dumps, telemetry upload, PIX/Nsight/RGP capture integration,
  performance measurement, or suppressing vendor/runtime messages by broad category.

### Deliverables

- Central diagnostics policy/checkpoint API and structured summary.
- Documented warning allowlist format with message ID, runtime/driver scope, reason, and owner; the
  initial committed list remains empty unless a real reviewed warning requires an entry.
- Opt-in InfoQueue policy probe and CPU tests for severity/allowlist classification.
- A validation log for create, 1,000-frame run, resize sequence, and teardown on the verified GPU.

### Tests

| Layer | Method |
|---|---|
| CPU | Feed synthetic severities/IDs through policy tests, including unknown Warning and any Error/Corruption as failures. |
| GPU | Run normal and GPU-Based Validation modes separately through 1,000 frames and resize; do not use either run for benchmarks. |
| Win32 | Execute startup, Present, resize/minimize/restore, close, and teardown with diagnostics checkpoints and a final live-object report. |

### Diagnostics acceptance

- **Debug Layer:** requested/effective state is unambiguous; normal and GPU-Based Validation runs
  complete with zero Error/Corruption and no unexplained lifecycle Warning.
- **InfoQueue:** the opt-in application-message probe proves collection and classification; normal
  validation ends with zero failing messages, and unknown Warnings fail rather than disappear.
- **DRED:** configuration ordering is asserted before device creation; the removal formatter is CPU
  tested for partial data, and all device-removal entry points attempt breadcrumb/page-fault output.
  Manufacturing a real TDR or page fault is not required and must not be automated.

### Exit and failure handling

Exit when policy CPU tests pass, both local validation modes complete, and the final live-object
report shows no owned D3D12/DXGI leaks. A diagnostics failure records the message ID, severity,
text, checkpoint, and frame when known, then returns nonzero. Fix the cause first; add a scoped
warning exception only with written evidence.

### Dependencies

Depends on Loops 1 through 5 so the audit covers the complete runtime. Loop 7 reuses the same
policy during deferred-deletion integration and final soak.

## Closed Loop 7: Deferred Deletion and Handle/Fence Retirement

### Scope

- Add a small generation-counted CPU handle pool with explicit invalid/stale-handle rejection.
- Define a retirement token as Direct queue identity plus submitted fence value. Values from
  different queue identities must never be compared, even though Stage 2 creates only one queue.
- Add a basic deferred-deletion queue that owns movable COM/object payloads until the associated
  Direct fence completes, then releases them deterministically.
- Support immediate release only for unsubmitted objects, failed creation, or after an explicit
  queue-idle boundary.
- Integrate collection at safe frame boundaries and complete collection after shutdown queue idle.
- Demonstrate at least one test-owned D3D12 object surviving CPU logical destruction until its
  retirement fence completes.

### Not included

- Descriptor allocation/reuse, persistent/transient heaps, multi-queue retirement, memory-budget
  eviction, background collector threads, Render Graph resource lifetimes, or D3D12MA allocation.

### Deliverables

- Pure CPU generation-handle and Direct-fence retirement components under an appropriate core/gfx
  boundary.
- Minimal deferred-deletion integration for D3D12 object ownership.
- Catch2 coverage for handle generation, stale access, retirement boundaries, queue identity,
  ordering, empty collection, and shutdown drain.
- Final Stage 2 GPU/Win32 soak mode and validation summary.

### Tests

| Layer | Method |
|---|---|
| CPU | Cover allocate/free/reuse generation changes, stale handles, invalid indices, fence `<`, `==`, and `>` boundaries, out-of-order retirement, wrong-queue rejection, and final drain. |
| GPU | Submit work referencing a named test object, retire it at the submission fence, prove it is retained before completion and released after completion, then run at least 10,000 present frames with periodic collection. |
| Win32 | Combine the soak with resize/minimize/restore and close; shutdown idles once and drains all retired objects without deadlock or leaks. |

### Diagnostics acceptance

- **Debug Layer:** no use-after-free, invalid descriptor/resource, allocator-sync, or live-object
  Error/Corruption during retirement and final drain.
- **InfoQueue:** zero Error/Corruption and no unexplained Warning over the complete soak; retirement
  diagnostics identify queue/fence/object name without suppressing runtime messages.
- **DRED:** any removal during the retirement demonstration or soak records the last submitted
  frame/retirement values and emits available breadcrumb/page-fault data.

### Exit and failure handling

Exit when all CPU tests pass in Debug and Release, the object-retention demonstration passes, and
the 10,000-frame resize/close soak finishes with zero diagnostics failures and no live owned
objects. A premature release, stale-handle acceptance, cross-queue comparison, or undrained item is
a hard failure. Preserve the smallest failing fence sequence; do not mask it with per-frame idle or
immediate release.

### Dependencies

Depends on Loop 4 for Direct-fence semantics and Loop 6 for the validation policy. It is the final
Stage 2 loop.

## Dependency Order

```text
Stage 1
  -> Loop 1: diagnostics bootstrap + factory + adapter
      -> Loop 2: device + Direct queue
          -> Loop 3: swap chain + RTV/DSV heap boundary
              -> Loop 4: three frame contexts + fence-gated reuse
                  -> Loop 5: resize + queue idle + rebuild
                      -> Loop 6: complete diagnostics audit
                          -> Loop 7: deferred deletion + retirement
```

Loops are intentionally closed in this order. CPU-only test code for a later loop may be designed
early, but it must not expand the runtime surface or be reported complete before its dependencies
pass.

## Stage 2 Completion Gate

Stage 2 is complete only when all seven loop exits are satisfied and the following combined gate
passes:

- Existing Stage 1 bootstrap and automation remain unchanged and functional.
- Debug and Release build, formal CPU tests, and Win32 smoke tests pass.
- The application selects and reports the verified hardware adapter and actual device features.
- Three frame contexts present and clear for at least 10,000 frames without a global per-frame GPU
  wait; allocators reset only after their own Direct-fence values complete.
- Grow, shrink, minimize/restore, and close paths rebuild or release resources only at defined
  queue-idle boundaries.
- Debug Layer and InfoQueue report zero Error/Corruption; Warnings are absent or narrowly documented
  by ID. DRED was enabled before device creation and all removal paths attempt to report it.
- Deferred deletion, generation handles, and fence retirement pass their CPU boundary tests and
  leave no owned live objects after final drain.
- No Render Graph, DXR pipeline, scene loader, ImGui, complex descriptor allocator, or multi-queue
  code has entered the stage.

If the combined gate fails, reopen the earliest loop that owns the violated invariant, retain a
minimal reproduction, and rerun all dependent loop checks after the fix.

## First Implementation Task

Begin Closed Loop 1 only: add the pre-device diagnostics bootstrap, create the debug DXGI factory,
enumerate/rank hardware adapters, emit the data-only adapter capability report, and add synthetic
CPU tests for selection. Do not retain a D3D12 device or create a queue or swap chain in that first
implementation change.
