# Donut NVRHI Rendering Lab: Detailed Implementation Plan

Status: **execution baseline**

Created: 2026-08-18

Source of intent: [`NEW_PLAN.md`](NEW_PLAN.md)

Current progress (2026-08-20): **S0.1 through S1.6 complete**; Stage 0 / M0 is satisfied;
Stage 1 is satisfied. The next step is **S2.1 — Define the lighting contract**.
Step evidence and commit hashes live in [`docs/PROGRESS.md`](docs/PROGRESS.md).
S1.5 implementation commit: `5664f3f124a50ed4f7259acaef1fa40c90922255`.
S1.6 implementation commit: `afecd9a7440b45c6c8620af12be8b8fbfc74c672`.

## 1. Purpose and Success Definition

This plan turns the revised project direction into small, verifiable implementation steps. The
project is successful when it demonstrates two systems inside a stable renderer:

1. a small render dependency graph (RDG) with explicit pass and resource semantics; and
2. a scene-level DXR architecture with explainable BLAS, TLAS, pipeline, and lifetime ownership.

The shortest useful path is:

```text
Donut/NVRHI application
  -> GBuffer
  -> deferred lighting
  -> tone mapping and present
  -> RDG migration
  -> DXR scene
  -> ray-traced visibility
```

The first portfolio-ready stopping point is **M4**, not the completion of every optional
experiment. Stages 8 and 9 deepen the result after the architecture is already demonstrable.

## 2. Rules of Execution

### 2.1 One closed loop at a time

Only one numbered step may be in progress. A step is complete only when its implementation,
verification, and documentation evidence are all present. The next step must not compensate for
an unfinished exit condition in the current step.

### 2.2 Commit and evidence policy

Each completed step should produce one focused commit or a short reviewable commit series. Update
[`docs/PROGRESS.md`](docs/PROGRESS.md) with:

- the step ID and completion date;
- the commit hash;
- commands that were run;
- screenshots, captures, graph dumps, or benchmark output when applicable;
- known limitations that are explicitly deferred.

Never mark a step complete using visual inspection alone when an automated assertion is practical.

### 2.3 Ownership boundaries

Donut/NVRHI owns:

- application lifecycle, window, swap chain, device, queues, fences, and command lists;
- physical buffers, textures, views, binding objects, pipelines, and command submission;
- shader build integration and scene/asset loading where its supported path is sufficient;
- low-level resource-state execution.

RenderLab owns:

- renderer passes and their parameter contracts;
- logical RDG resources, versions, dependencies, culling, lifetimes, and state intent;
- DXR scene policy, geometry/instance mapping, AS scheduling, SBT policy, and statistics;
- tests, debug views, graph dumps, captures, and reproducible benchmarks.

RenderLab must not recreate a second device, swap chain, queue, fence, command-list abstraction, or
generic RHI. Any unavoidable Donut/NVRHI source modification requires a separate commit and an ADR.

### 2.4 Scope controls

The following are excluded before M4:

- TAA, temporal history, dynamic resolution, bloom, and a general post-processing stack;
- a general material graph, editor, ECS, animation system, or asset pipeline;
- multi-queue RDG scheduling, enhanced-barrier specialization, or a general heap allocator;
- ReSTIR, RTGI, denoising, path tracing, and more than the minimum required ray types;
- modifications made only to increase feature count rather than validate RDG or DXR architecture.

Optional work is identified explicitly and may not block a stage gate.

### 2.5 Failure time boxes

- If an upstream integration problem consumes two focused work sessions, reduce it to a minimal
  reproduction and verify the intended API against the pinned upstream revision.
- If a step grows beyond roughly three working days, split it before adding more behavior.
- If Donut already provides a feature that is outside the learning goal, use it.
- If Donut already provides a feature that is the learning goal, place the custom implementation
  above NVRHI and document the boundary instead of silently mixing both systems.

## 3. Stage and Milestone Map

| Stage | Target | Estimate at 10-15 h/week | Gate |
|---|---|---:|---|
| 0 | Pinned Donut/NVRHI application baseline | 1 week | M0 |
| 1 | Inspectable GBuffer | 1-2 weeks | |
| 2 | Deferred HDR lighting | 1 week | |
| 3 | Tone-mapped raster pipeline | 0.5-1 week | M1 |
| 4 | CPU-side RDG compiler | 1.5-2 weeks | |
| 5 | RDG execution and raster migration | 1.5-2 weeks | M2 |
| 6 | DXR scene and AS lifecycle | 1.5-2.5 weeks | M3 |
| 7 | RDG ray-traced visibility pass | 1-2 weeks | M4 |
| 8 | DXR policy experiments | 1-2 weeks | optional depth |
| 9 | Reproducible report and portfolio package | 1-2 weeks | M5 |

M4 should be reachable in approximately 10-13 weeks. M5 is the intended 12-16 week result. These
are capacity estimates, not permission to waive exit conditions.

## 4. Stage 0 - Framework Baseline

### S0.1 - Select and record the upstream baseline

**Goal:** choose a known Donut revision and understand exactly which NVRHI revision and build
options it brings.

**Actions:**

1. Inspect recent stable Donut revisions, their NVRHI submodule revision, D3D12 support, required
   CMake version, shader compiler path, and sample applications.
2. Select one full 40-character Donut commit. Do not track a branch or floating tag.
3. Record the Donut and transitive NVRHI commits in `dependencies.lock.json`.
4. Record the validated Visual Studio, MSVC, Windows SDK, CMake, GPU driver, and PIX versions in
   `docs/build-environment.md`.
5. Write `docs/adr/ADR-001-donut-nvrhi-baseline.md`, including why this revision was selected and
   which Donut application class/sample will serve as the integration reference.
6. Decide whether Donut's own dependency-fetch mechanism or recursive Git submodules is the sole
   bootstrap path. Do not introduce vcpkg unless a RenderLab-owned dependency actually requires it.

**Verification:** every dependency entry is an exact commit/version; the chosen Donut revision and
its recursive submodules can be checked out on Windows; no placeholder such as `latest`, `main`, or
`TBD` remains in a lock file.

**Exit condition:** the baseline and acquisition method are reviewable without cloning anything
implicitly.

### S0.2 - Add Donut as the pinned external dependency

**Goal:** make a clean checkout obtain exactly the baseline selected in S0.1.

**Actions:**

1. Add Donut under `external/donut` as a Git submodule at the locked commit.
2. Initialize its required recursive submodules using one documented command or a small bootstrap
   script.
3. Configure only the Donut/NVRHI options needed for a D3D12 desktop application; disable samples,
   tests, Vulkan, and unrelated tools when upstream supports doing so safely.
4. Adapt root CMake without copying Donut or NVRHI source into RenderLab targets.
5. Update CI checkout to include recursive submodules and add a configure-only job first.
6. Add upstream licenses to `THIRD_PARTY_NOTICES.md`.

**Verification:** a fresh recursive clone configures in Debug and Release; `git submodule status
--recursive` contains no leading `-` or `+`; CMake prints the intended D3D12 backend only.

**Exit condition:** dependency acquisition is deterministic and generates no tracked files.

### S0.3 - Create the minimal RenderLab application

**Goal:** own a small application entry point while delegating platform and GPU bootstrap to Donut.

**Actions:**

1. Create `src/app/RenderingLabApp.*` and the smallest executable entry point supported by the
   pinned Donut revision.
2. Open a window, clear the back buffer, render the basic UI, and present continuously.
3. Add command-line options for `--scene`, `--headless`, `--frames`, and `--output`; options may
   report a clear "not implemented" error until their associated steps, but parsing must be stable.
4. Print adapter name, driver version when available, NVRHI backend, DXR tier, and shader model.
5. Keep RenderLab source free of direct device, queue, fence, and swap-chain creation.

**Verification:** Debug and Release build; the application opens, clears, resizes, and closes; a
60-second validation run has no NVRHI or D3D12 error; unsupported DXR hardware produces a clear
capability report but does not prevent raster startup.

**Exit condition:** RenderLab owns the app behavior while Donut/NVRHI unambiguously owns the low
level runtime.

### S0.4 - Load one fixed scene and camera

**Goal:** establish the scene input used throughout the raster and DXR vertical slices.

**Actions:**

1. Select a small redistributable glTF scene with multiple meshes and basic metallic-roughness
   materials; record URL, license, and SHA-256 in `scenes/manifest.json`.
2. Add a download/verification script only if the scene is too large to commit.
3. Load the scene through Donut's scene facilities.
4. Add a deterministic camera preset and a command-line switch that disables free-camera motion.
5. Define a tiny committed fallback scene or fail with an actionable missing-asset message.

**Verification:** interactive mode loads the scene; the fixed camera is identical after restart;
asset corruption is detected; no absolute developer-machine path appears in output.

**Exit condition:** one deterministic scene/camera pair can drive later image tests.

### S0.5 - Establish observability and capture

**Goal:** ensure every later pass can be inspected before renderer complexity is added.

**Actions:**

1. Add stable CPU/GPU marker names for frame, scene update, render, UI, and present.
2. Add a small runtime diagnostics panel with adapter, resolution, frame number, and validation mode.
3. Confirm a PIX GPU capture can be started and inspected.
4. Add a fixed-frame smoke command and a CI-safe startup mode if the runner supports the required
   graphics environment; otherwise keep it as a documented local test.
5. Save one M0 capture checklist in `docs/capture-guide.md` without committing a large capture file.

**Verification:** PIX displays a readable frame hierarchy; marker names remain stable across Debug
and Release; validation produces no error/corruption messages.

**Stage 0 gate - M0:** a fresh clone builds, loads the fixed glTF scene, renders a stable Donut/NVRHI
D3D12 frame, and can be captured without modifying upstream code.

## 5. Stage 1 - GBuffer Rasterization

S1.1 through S1.6 are complete. Stage 1 is satisfied: GBuffer channels are
inspectable, timed, documented, and protected by a repeatable capture. Next is
S2.1.

### S1.1 - Freeze renderer conventions and the GBuffer contract

**Goal:** eliminate coordinate, color, and encoding ambiguity before shader work.

**Actions:**

1. Write `docs/renderer-conventions.md`: handedness, matrix convention, front face, UV origin,
   world/view spaces, NDC depth, reversed-Z choice, HDR units, and linear/sRGB boundaries.
2. Write `docs/g-buffer.md` with exact first-version formats and channel layout.
3. Use the minimum required targets: base color, world normal plus roughness, metallic/AO/material
   flags, and depth. Do not add velocity before a real consumer exists.
4. Define clear values and background semantics for every channel.
5. Record the format decision in `docs/adr/ADR-002-gbuffer-layout.md`.

**Verification:** each deferred-lighting input has exactly one documented source; formats are
supported as the required RTV/SRV/depth views on the target GPU.

**Exit condition:** the CPU structs and HLSL declarations can be implemented without inventing
additional conventions.

### S1.2 - Define frame, view, instance, and material data

**Goal:** establish explicit data contracts instead of pass-specific ad hoc constants.

**Actions:**

1. Add shared C++ structures for frame/view constants and RenderLab-owned material parameters.
2. Add matching HLSL structures with documented alignment and matrix convention.
3. Map Donut scene meshes/materials/transforms into lightweight renderer-facing draw records.
4. Provide opaque fallback textures/values only for glTF fields that are legally optional; log
   unsupported required data.
5. Add CPU assertions/tests for layout, alignment, and deterministic material conversion.

**Verification:** at least two materials and two transforms produce distinct, correct draw records;
C++ and HLSL sizes/offsets are documented or asserted.

**Exit condition:** GBuffer code consumes renderer data contracts rather than Donut internals
throughout the shader path.

### S1.3 - Create persistent GBuffer targets and resize handling

**Goal:** create the first physical render resources with correct lifetime and view flags.

**Actions:**

1. Create GBuffer/depth texture ownership in the renderer using NVRHI resources.
2. Recreate size-dependent resources on resize only after the previous resources are safe to
   release under the Donut/NVRHI lifetime model.
3. Assign debug names that match the documented channel names.
4. Clear all targets to defined values and expose their SRV handles to later passes.
5. Report dimensions, formats, and approximate allocation size in the debug UI.

**Verification:** repeated resize/minimize/restore is stable; PIX shows the expected formats and
dimensions; validation reports no use-after-free or state error.

**Exit condition:** target creation and destruction are independent of drawing logic.

### S1.4 - Implement the opaque GBuffer pass

**Goal:** rasterize scene geometry into all mandatory GBuffer channels and depth.

**Actions:**

1. Add `GBufferPass` with explicit input/output parameters.
2. Create the binding layout, graphics pipeline, vertex shader, and pixel shader.
3. Draw opaque scene meshes using the fixed camera and instance transforms.
4. Write base color, normalized world-space normal, roughness, metallic, AO/default, and depth.
5. Add a `GBuffer` GPU marker and timestamp scope.

**Verification:** a capture shows MRT and depth writes; foreground/background clear values are
correct; camera and transformed instances align; no validation errors occur.

**Exit condition:** the fixed scene produces stable GBuffer data without lighting.

### S1.5 - Add GBuffer debug visualization

**Goal:** make every encoded value visually diagnosable.

**Actions:**

1. Add debug modes for base color, normals, roughness, metallic, AO/material flags, and linearized
   depth.
2. Display the selected channel name and decode convention in the UI.
3. Add numeric pixel inspection if Donut provides a low-cost path; otherwise defer it explicitly.
4. Add a deterministic screenshot command for every mandatory view.

**Verification:** normal colors change consistently with surface orientation; roughness/metallic
match scene materials; reconstructed/linearized depth is monotonic and finite.

**Exit condition:** each GBuffer defect can be isolated without editing a shader.

### S1.6 - Create the first image-regression baseline

**Goal:** protect raster correctness before lighting and RDG refactors.

**Actions:**

1. Add headless or fixed-frame capture for the fixed scene, camera, resolution, and frame number.
2. Store compact reference images or their approved hashes/metrics under `tests/golden/`.
3. Define per-output comparison rules; avoid exact byte equality for floating-point views.
4. Record adapter/driver metadata alongside results and separate portability failures from real
   renderer regressions.

**Verification:** an unchanged build passes twice; a deliberate channel swap fails; output paths
are deterministic and ignored/generated artifacts do not dirty Git.

**Stage 1 gate:** all required GBuffer channels are independently inspectable, timed, documented,
and protected by a repeatable capture.

## 6. Stage 2 - Deferred HDR Lighting

### S2.1 - Define the lighting contract

**Goal:** set a deliberately small physically based lighting scope.

**Actions:** define one directional light, a bounded number of point lights, camera reconstruction,
Lambert diffuse, simplified GGX specular, attenuation, exposure-independent HDR output, and the
background policy. Document units and approximations in `docs/lighting.md`.

**Verification:** every term has a known space and unit; no shadow or DXR input is required.

**Exit condition:** the shader interface is frozen for the first lighting pass.

### S2.2 - Implement position reconstruction and a diagnostic light

**Goal:** validate depth and normal consumption before full BRDF complexity.

**Actions:** create the HDR scene-color target; reconstruct position from depth; implement a
normal/distance diagnostic light; add debug modes for reconstructed position and `N dot L`.

**Verification:** reconstructed surfaces remain fixed as the camera moves; background pixels do not
produce NaN/Inf; the diagnostic light agrees with GBuffer normals.

**Exit condition:** depth reconstruction is trusted independently of BRDF output.

### S2.3 - Implement directional and point-light shading

**Goal:** produce a stable HDR image from the documented GBuffer.

**Actions:** add light-buffer upload, Lambert plus GGX evaluation, directional light, point lights,
and optional ambient constant; bracket the pass with marker/timestamp; avoid clustered/tiled light
culling.

**Verification:** material parameter changes have expected effects; zero lights produce the defined
ambient/background; light counts at the supported limit are bounds-safe.

**Exit condition:** GBuffer to HDR lighting is stable without DXR.

### S2.4 - Add lighting validation and regression output

**Goal:** catch math, color-space, and integration regressions.

**Actions:** add CPU tests for BRDF helper edge cases where practical; capture HDR and a viewable
tonemapped diagnostic; detect non-finite pixels; save timing and configuration metadata.

**Verification:** grazing angles and minimum roughness remain finite; the fixed scene repeats within
the selected tolerance; capture inspection shows only declared GBuffer reads and HDR writes.

**Stage 2 gate:** deferred lighting consumes documented data and produces a validated HDR target.

## 7. Stage 3 - Minimal Post Processing and M1

### S3.1 - Freeze exposure and output-transfer policy

**Goal:** define exactly where HDR becomes display-referred output.

**Actions:** choose manual exposure, one tone mapper, back-buffer format, and hardware-versus-shader
sRGB encoding responsibility; document the policy; explicitly postpone automatic exposure,
bloom, color grading, FXAA, and TAA.

**Verification:** there is exactly one gamma/output transfer; the UI composition space is known.

**Exit condition:** implementation has no ambiguous double-encoding path.

### S3.2 - Implement tone mapping and present

**Goal:** complete the first end-to-end renderer.

**Actions:** add `PostProcessPass`, exposure parameter, tone-map shader/fullscreen pass, UI overlay,
resize handling, marker, and timestamp; make normal rendering select the final output while retaining
all debug views.

**Verification:** exposure changes are predictable; highlights roll off without clipping the whole
image; resize and long-run validation are clean.

**Exit condition:** `Scene -> GBuffer -> Deferred -> Tone Map -> Present` is the normal path.

### S3.3 - Freeze the manual-pipeline reference

**Goal:** preserve a known-correct oracle for the RDG migration.

**Actions:** capture final and intermediate reference outputs; export per-pass GPU timing; document
manual pass ordering and resource states; tag or record the M1 commit; do not add raster features
after this point unless required by RDG/DXR.

**Verification:** Debug and Release pass the smoke/regression suite and a PIX frame has readable,
separate GBuffer, lighting, post, and present scopes.

**Stage 3 gate - M1:** a stable, inspectable raster renderer exists and is frozen as the RDG
migration reference.

## 8. Stage 4 - CPU-Side Mini RDG Compiler

Stage 4 is GPU-independent by design. Build and test graph semantics before letting the graph touch
NVRHI resources.

### S4.1 - Define handles, descriptors, and pass declarations

**Goal:** create the smallest type-safe logical model.

**Actions:** add typed texture/buffer handles with generation checks; logical resource descriptors;
pass records; explicit read/write declarations; imported/exported resource flags; side-effect and
never-cull flags. Write `docs/rdg.md` and ADR-003 for the RDG boundary.

**Verification:** stale, null, cross-graph, and type-mismatched handles fail deterministically in
Debug/tests; no NVRHI header is required by the compiler model where avoidable.

**Exit condition:** tests can construct graphs without a GPU device.

### S4.2 - Implement resource versioning

**Goal:** make every write create an explicit new logical version.

**Actions:** model initial/imported versions; return a new handle/version from writes; retain
producer and reader lists; reject read-before-produce, duplicate incompatible writes, and use of a
superseded version where the API forbids it.

**Verification:** tests cover single writer, read chain, multi-reader, WAW, read-before-write,
imported input, and exported output.

**Exit condition:** a textual dump explains which pass produced every consumed version.

### S4.3 - Build dependencies and topologically sort

**Goal:** compile declarations into a deterministic pass DAG.

**Actions:** generate RAW/WAR/WAW ordering edges from version/access semantics; deduplicate edges;
topologically sort with a stable tie-breaker; report cycles with pass/resource names; keep algorithm
code separate from execution code.

**Verification:** tests cover linear, fan-in, fan-out, independent, overwrite, and intentional cycle
graphs; repeated compilation produces identical order and dump.

**Exit condition:** all live passes have a deterministic legal order or a diagnostic compile error.

### S4.4 - Implement output-driven pass culling

**Goal:** remove work that cannot affect an exported resource or declared side effect.

**Actions:** mark roots from outputs/side effects; walk dependencies backward; record cull reason;
add global disable-culling and per-pass never-cull debug controls.

**Verification:** tests cover unused leaf, unused chain, side-effect pass, shared producer, forced pass,
and disabled-culling modes.

**Exit condition:** dumps list live and culled passes with a reason.

### S4.5 - Analyze logical lifetimes

**Goal:** calculate first/last use after final scheduling and culling.

**Actions:** compute first/last use for each live version and aggregate resource lifetime; define
imported/exported lifetime semantics; reject zero-use non-imported allocations; expose interval data
without allocating physical resources.

**Verification:** tests cover overlapping, non-overlapping, imported, exported, culled, and
read-only lifetimes.

**Exit condition:** every live logical resource has a deterministic interval.

### S4.6 - Add compiler diagnostics and graph export

**Goal:** make graph behavior observable before GPU integration.

**Actions:** emit stable text and DOT dumps containing pass order, edges, resource versions,
accesses, cull state, and lifetimes; add compile error categories; expose `--dump-rdg`; add golden
tests for a small representative graph.

**Verification:** Graphviz can render the DOT output; diagnostics name both pass and resource;
golden output is deterministic.

**Stage 4 gate:** the CPU compiler correctly versions, orders, culls, and analyzes a synthetic graph
with comprehensive unit tests.

## 9. Stage 5 - RDG Execution and Raster Migration

### S5.1 - Define imported resources and execution context

**Goal:** connect logical graph data to NVRHI without exposing global renderer state.

**Actions:** define physical-resource registry, import back buffer/scene-owned resources, export
final outputs, and create a pass execution context that resolves only declared handles. Forbid
undeclared lookup in Debug builds.

**Verification:** a synthetic GPU pass resolves declared resources; undeclared and stale access
fails before command recording.

**Exit condition:** pass callbacks receive no unrestricted graph or renderer resource table.

### S5.2 - Allocate physical textures and buffers

**Goal:** instantiate internal resources from logical descriptors at graph compile/execute time.

**Actions:** map descriptors to NVRHI descriptors; validate usage flags and formats; cache/reuse only
exact compatible resources initially; attach debug names and allocation statistics; defer complex
heap aliasing.

**Verification:** create/destroy/resize graphs repeatedly; reported resources match PIX; incompatible
reuse is rejected.

**Exit condition:** logical and physical resources have separate, inspectable identities.

### S5.3 - Implement access-state planning

**Goal:** translate RDG access intent into an auditable NVRHI state plan.

**Actions:** define the initial access enum subset actually used by raster passes; map each access to
NVRHI state; compute per-pass before/after requirements; validate conflicting same-pass access;
issue or declare states through one documented NVRHI path. Do not maintain a competing hidden state
tracker.

**Verification:** unit tests cover transitions, UAV ordering where required, repeated read state,
invalid combinations, imported initial/final state, and unknown access; capture agrees with the dump.

**Exit condition:** the graph dump can explain every raster resource state request.

### S5.4 - Migrate tone mapping first

**Goal:** validate RDG execution on the smallest leaf pass.

**Actions:** import manual HDR input and back buffer; declare tone-map read/output write; execute the
existing pass unchanged where possible; compare against the manual result; retain a temporary
manual/RDG switch.

**Verification:** image comparison passes and the graph dump has one live graphics pass with correct
imports/exports.

**Exit condition:** RDG controls order and resources for tone mapping.

### S5.5 - Migrate deferred lighting

**Goal:** validate multi-input reads and an internal HDR output.

**Actions:** import manual GBuffer/depth; create HDR through RDG; declare lighting reads/write;
execute lighting then tone map; compare manual and RDG modes; test culling of an unused lighting
debug branch.

**Verification:** intermediate HDR and final output match reference tolerances; culling and state
plans are visible.

**Exit condition:** RDG owns the lighting-to-present chain.

### S5.6 - Migrate GBuffer and remove the manual scheduler

**Goal:** make the entire raster frame graph-controlled.

**Actions:** create all GBuffer/depth resources through RDG; declare raster writes and later reads;
route debug views as selectable outputs; validate resize; remove the production manual scheduling
path after parity is proven.

**Verification:** all M1 image tests pass; Debug/Release and PIX validation are clean; the graph dump
contains GBuffer, deferred, selected debug/output path, tone map, and present dependencies.

**Exit condition:** no production raster pass bypasses RDG resource declaration.

### S5.7 - Add conservative transient reuse and statistics

**Goal:** demonstrate lifetime-driven reuse without building a general heap allocator.

**Actions:** pool exact-compatible physical resources whose logical lifetimes do not overlap; keep
imported/exported resources ineligible; expose reuse pairs, saved allocation estimate, and peak
logical/physical bytes; add a disable-reuse switch.

**Verification:** interval tests prove overlap rejection; images match with reuse on/off; peak
physical bytes never exceed the non-reuse baseline for the same graph.

**Stage 5 gate - M2:** the raster pipeline is graph-scheduled with declared access, deterministic
compilation, culling, lifetime analysis, state planning, debug export, and basic transient reuse.

## 10. Stage 6 - DXR Scene and Acceleration Structures

### S6.1 - Define DXR capability and scene contracts

**Goal:** introduce DXR types without building an AS yet.

**Actions:** add `RTGeometry`, `RTInstance`, stable handles, dirty flags, build-policy enums, and
`RayTracingScene`; document ownership/state machines in `docs/dxr-architecture.md`; add CPU tests
for handle lifetime, transform conversion, masks, and instance descriptor generation.

**Verification:** scene registration/removal is deterministic; row/column and 3x4 transform
conversion tests use known points.

**Exit condition:** DXR scene policy is testable without command submission.

### S6.2 - Map Donut scene geometry into RT geometry

**Goal:** create a verified mapping from raster geometry buffers to NVRHI ray-tracing geometry
descriptors.

**Actions:** support indexed opaque triangles first; validate buffer format/offset/stride/count;
classify static versus transform-only dynamic data; reject unsupported skinned, procedural, or
alpha-tested geometry with named diagnostics rather than silently omitting it.

**Verification:** descriptor counts/bounds match raster mesh metadata for the fixed scene.

**Exit condition:** every supported raster mesh has one explainable RT geometry record.

### S6.3 - Build and retain static BLAS

**Goal:** create one stable BLAS per supported geometry set using NVRHI.

**Actions:** implement registry state transitions; query/record allocation and scratch requirements
where exposed; schedule builds with named markers; retain results across frames; defer compaction;
release scratch/results through the existing NVRHI lifetime model.

**Verification:** BLAS builds once for an unchanged scene; validation and PIX AS inspection are
clean; rebuild is triggered by a deliberate topology change only.

**Exit condition:** static BLAS persists without per-frame global GPU waits.

### S6.4 - Build the main TLAS from RT instances

**Goal:** represent the fixed scene as one traceable top-level scene.

**Actions:** generate instance descriptors with transform, ID, mask, contribution/hit-group offset,
and BLAS reference; upload the instance buffer; build one main TLAS; expose TLAS handle and build
statistics through `RayTracingScene`.

**Verification:** instance count/transforms match the raster scene; PIX shows valid BLAS/TLAS;
repeated unchanged frames do not rebuild unnecessarily.

**Exit condition:** the fixed scene owns a stable main TLAS.

### S6.5 - Add transform updates and deferred retirement

**Goal:** make cross-frame AS lifecycle correct before ray dispatch depends on it.

**Actions:** track dirty instance transforms; select TLAS update versus rebuild using explicit policy;
keep old buffers/AS alive until relevant GPU work completes; test add/remove/transform changes;
report update/rebuild reason per frame.

**Verification:** moving an instance changes its descriptor and TLAS without device-idle waits;
rapid scene reset/resize/shutdown has no lifetime error.

**Exit condition:** dynamic transforms and AS retirement are fence-safe under the chosen NVRHI path.

### S6.6 - Express AS work in RDG

**Goal:** order scene preparation and consumers through graph declarations.

**Actions:** extend access intent only with the AS build/write and ray-tracing read states required;
import persistent BLAS/TLAS physical objects using logical proxy resources; add BLAS/TLAS preparation
passes with explicit side effects or outputs; keep AS policy in `raytracing`, not `rdg`.

**Verification:** graph order guarantees builds/updates before a synthetic TLAS consumer; culling
cannot remove required AS work; graph dumps do not contain hit-group/material knowledge.

**Stage 6 gate - M3:** a static scene and transform-updated instances produce a correctly ordered,
persistent, observable BLAS/TLAS scene without global per-frame waits.

## 11. Stage 7 - First Ray-Tracing Pass

### S7.1 - Freeze the first ray contract

**Goal:** minimize pipeline and SBT complexity.

**Actions:** choose one hard-shadow/visibility ray; define ray generation inputs, one miss, one
closest-hit, one hit group, one ray type, minimal payload, recursion depth, mask, and output format;
document C++/HLSL/SBT alignment and local/global binding ownership.

**Verification:** payload and SBT record layouts have CPU size/alignment tests; no unused payload
field or ray type is included.

**Exit condition:** pipeline creation has a finite, documented contract.

### S7.2 - Create RT pipeline and shader binding table

**Goal:** construct and validate RTPSO/SBT independently of final shading integration.

**Actions:** compile raygen/miss/closest-hit shaders; create pipeline; build stable SBT records;
validate shader export and hit-group mapping; rebuild SBT only when its inputs change; expose record
count/stride/bytes in diagnostics.

**Verification:** CPU tests cover record offsets and out-of-range mapping; the target GPU accepts the
pipeline and SBT with validation enabled.

**Exit condition:** an empty/minimal dispatch can be issued without resource lifetime errors.

### S7.3 - Implement the RDG ray-traced visibility pass

**Goal:** generate a screen-space visibility texture from GBuffer depth/normal and TLAS.

**Actions:** declare depth, normal, camera, TLAS reads and UAV output; reconstruct world position;
offset ray origins using a documented bias; dispatch at viewport resolution; add marker/timestamp and
debug output; handle background pixels explicitly.

**Verification:** a no-occluder setup is fully visible; an occluder creates the expected hard shadow;
debug output is finite and stable; RDG orders TLAS work before dispatch.

**Exit condition:** ray output can be viewed independently and is graph-scheduled.

### S7.4 - Composite visibility in deferred lighting

**Goal:** connect DXR output to the existing renderer with minimal coupling.

**Actions:** add visibility as an optional declared lighting input; apply it to direct light only;
add runtime raster-only versus ray-traced toggle; ensure disabling DXR removes/culls AS and ray work
when policy permits.

**Verification:** raster-only output still matches M1/M2 reference; enabled output changes only
visibility; graph dump and timing show the expected conditional branch.

**Exit condition:** DXR is an optional renderer feature, not a startup requirement.

### S7.5 - Validate the complete M4 path

**Goal:** freeze the first portfolio-ready architecture result.

**Actions:** run long validation, resize, camera, instance transform, scene reload, raster/DXR toggle,
Debug/Release, screenshot regression, and PIX capture; export one representative RDG DOT graph and
AS/timing report; document known artifact/bias limitations.

**Verification:** no validation errors; TLAS build/update precedes dispatch; visibility precedes
lighting; final output and debug views are reproducible; no pass performs undeclared resource access.

**Stage 7 gate - M4:** the graph schedules raster, AS, ray dispatch, lighting, post-processing, and
present; the result is explainable and ready to demonstrate.

## 12. Stage 8 - Focused DXR Architecture Experiments

Complete these in order, but stop after any two high-quality experiments if schedule pressure would
otherwise delay Stage 9.

### S8.1 - BLAS/TLAS update versus rebuild policy

Add explicit policy switches and reason codes; separate topology, vertex, transform, and instance-set
dirty states; record AS time, trace time, total GPU time, and memory. Verify equal rendered output
before comparing performance.

### S8.2 - Material and hit-group mapping

Introduce stable material-to-hit-group mapping and SBT invalidation rules. Support opaque materials
first. Test reorder, add/remove, and material replacement without corrupting instance contribution
offsets.

### S8.3 - Inline RayQuery comparison

Implement the same visibility semantics using a compute/pixel `RayQuery` path. Keep inputs, output,
bias, resolution, scene, and warmup identical. Compare image equivalence before GPU time.

### S8.4 - Optional alpha-tested geometry

Only after S8.2 is stable, add one alpha-tested scene and any-hit path. Document texture binding,
opacity cutoff, geometry flags, SBT effect, and performance. This step is optional and must not delay
M5.

### S8.5 - Consolidate AS/SBT diagnostics

Export JSON containing policy, build/update reason, geometry/instance counts, BLAS/TLAS bytes,
scratch bytes, SBT bytes, and per-pass GPU times. Add an RT scene debug UI and validate counters
against a PIX capture.

**Stage 8 gate:** at least two controlled strategy comparisons have equivalent output, recorded
configuration, and interpretable timing/memory results.

## 13. Stage 9 - Reproducible Benchmark and Portfolio Package

### S9.1 - Freeze benchmark protocol

**Goal:** ensure measurements answer a defined question.

**Actions:** fix scene, camera path, resolution, warmup, sample frames, synchronization policy, build
configuration, validation state, GPU clocks/power notes, driver, and output schema; separate CPU and
GPU time; define median and percentile statistics; reject runs with setup/shader compilation noise.

**Exit condition:** another developer can reproduce a run from a clean checkout and one documented
command.

### S9.2 - Run two primary experiment matrices

Run at minimum:

1. TLAS/BLAS update versus rebuild for one controlled dynamic workload; and
2. pipeline `TraceRay` versus inline `RayQuery` for identical visibility output.

Record raw JSON/CSV, environment, image-equivalence evidence, at least five repeated runs when
variance requires it, and a short interpretation that distinguishes AS, trace, shading, and sync
costs.

**Exit condition:** conclusions follow from stored data and include limitations, not just averages.

### S9.3 - Produce architecture artifacts

Create and verify:

- module/ownership diagram;
- representative RDG DAG;
- logical resource lifetime/reuse diagram;
- BLAS registry and TLAS update state diagrams;
- one annotated PIX frame hierarchy;
- performance plots tied to raw result files.

**Exit condition:** every diagram matches the final code and uses the same component names.

### S9.4 - Write the technical report and reproduction guide

Write a self-contained report covering motivation, boundaries, implementation evolution, failed or
rejected approaches, correctness evidence, performance method/results, and next work. Update README
with build, run, capture, graph dump, test, and benchmark commands. Audit third-party licenses and
remove stale dependencies/artifacts.

**Verification:** follow the README from a clean directory; all links and commands work; the report
can be viewed without a local web server if HTML is used.

**Stage 9 gate - M5:** the repository is reproducible, technically defensible, and ready for a code
review, interview walkthrough, or portfolio presentation.

## 14. Required Test Matrix

| Area | Required automated coverage | Required GPU/manual evidence |
|---|---|---|
| Data contracts | struct size/alignment, material conversion | PIX buffer inspection |
| GBuffer | capture command and image comparison | all channel debug views |
| Lighting/post | finite math, fixed output comparison | HDR and final screenshots |
| RDG handles | null, stale, type, graph ownership | diagnostic text |
| RDG compiler | versioning, RAW/WAR/WAW, sort, cycle, culling | DOT graph |
| RDG lifetime | first/last use, overlap, imported/exported | reuse statistics |
| RDG state intent | access mapping, invalid combinations | PIX/state dump agreement |
| RT scene | transforms, masks, IDs, dirty policy | PIX BLAS/TLAS inspection |
| SBT | alignment, offsets, mapping, invalidation | pipeline creation/dispatch |
| Integration | fixed scene/camera image regression | validation run and capture |
| Benchmark | schema/statistics tests | raw runs and environment record |

## 15. Definition of Done for Any Step

A step is done only when all applicable items are true:

- the scoped behavior works in Debug and Release;
- relevant CPU tests and image/GPU validation pass;
- D3D12/NVRHI validation has no new error or corruption message;
- resources and passes have stable debug names;
- failure produces an actionable diagnostic rather than silent fallback;
- documentation and `docs/PROGRESS.md` reflect the actual implementation;
- generated files, captures, downloaded scenes, and results do not dirty the repository;
- no unrelated feature or abstraction was added;
- a focused commit preserves the closed loop.

## 16. Immediate Next Action

Start **S0.1 only**. Do not create the application, GBuffer files, RDG interfaces, or DXR types until
the Donut/NVRHI baseline, acquisition method, build options, and ownership boundary have been
verified and recorded.
