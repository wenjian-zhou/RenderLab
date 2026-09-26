# RDG Alignment With UE

Status: ready to execute. Do this in order: Phase 1, then Phase 2. Do not start Phase 2 until Phase 1's Debug and Release CPU tests are green. Do not expand this into call-site thinning (factory-only declarations, snapshot query, one-line lambdas), a cross-frame pool, or moving framebuffer setup out of the pass.

Source checked: UE 5.8.1 at `D:\UnrealEngine` (`Engine/Build/Build.version`). UE keeps one `FRDGTexture*` for the whole graph. A write does not return a new resource. `FRDGBuilder::MarkResourcesAsProduced` (`RenderGraphBuilder.cpp` around line 2238) sets `bProduced` on that same object. Later passes name the same pointer. Order is `AddPass` order. Producer links are last-writer to later reader. `CreateUAV` / `CreateSRV` return views, not new texture identities. This plan does not add views.

## Decisions already made

- One resource identity per `Create*` / `Import*`. `TextureHandle` and `BufferHandle` stay `{index, graphId}`. Delete the `version` field.
- `PassBuilder::Read` / `Write` go away. A pass declares `Use(handle, access)`. `Write` no longer returns a handle. Call sites stop assigning `Write` results back onto the handle.
- `Access` on the use is the declaration. `Plan()` does not infer access from a read/write bit plus format. Delete `AccessMode` and `InferAccess`.
- A use access is one of `ShaderResource`, `RenderTarget`, `DepthWrite`. `Present` is not a pass use. `Present` is only an initial or final boundary access.
- Writable uses are `RenderTarget` and `DepthWrite` (`kWritableMask`). `ShaderResource` is a read. A read with no earlier writable use, on a resource that is not imported, is still `ReadBeforeProduce`.
- Two uses of the same resource in one pass, when either is writable, are `DuplicateWrite` or `IncompatibleAccess` (same conflict rule as today). A later pass may write the same resource again. That later write does not keep the earlier writer alive.
- Pass order is `AddPass` order. Live order is ascending pass index among passes that survive culling. There is no topological sort and no cycle diagnostic.
- A producer edge exists only from the last earlier writer of a resource to a later reader of that resource. No RAW/WAR/WAW labels. No edge between two writers. No edge from a reader to a later writer.
- Culling roots stay: `NeverCull`, or a writable use of an imported or exported resource. The walk still un-culls the producer closure. An overwritten writer is culled when nothing reads it before the next write.
- `Compile()` stays a public CPU step. The app still calls `Compile`, `SetDevice`, `RegisterImport`, `Allocate`, `Plan`, `Execute`. Do not fold that sequence into one call.
- Collected `rdg::Error`s stay. Do not switch to `checkf`.
- Cull reasons, text dumps, and DOT dumps stay. Update their contents to the grammar below. `--dump-rdg` still writes the M1-shaped graph.
- Lifetime rules stay: imported first/last use, exported last-use extended through the last live pass, non-imported zero-use is `ZeroUseAllocation`. Drop per-version lifetime lines. S5.7 reuse still keys off those resource lifetimes. Do not change the reuse match (format + size, non-overlapping live slots, import/export ineligible, `--no-transient-reuse`).
- `RegisterImport` rejects a native pointer already bound to a different resource index (`IncompatibleAccess`). The same handle and the same native is a no-op. Logical `Import*` still does not see the pointer; dedup is at `RegisterImport`, because compile stays device-free.
- `ExportTexture` / `ExportBuffer` take the final `Access`. `SetInitialAccess` is required for imported resources before `Plan()`. `Create*` initial access stays the `FormatMap` allocation state: color `RenderTarget`, `D32Float` `DepthWrite`, buffer `ShaderResource`.
- Phase 2 issues those states with `setTextureState` / `setBufferState` and `commitBarriers` inside `Execute`, before the pass lambda. `Execute(nullptr)` records the same transition log and does not call NVRHI. `Execute` does not require `Plan()`; if `Plan()` did not succeed, it runs lambdas and issues no barriers, matching today's tests that execute without a plan.
- RDG `Create*` descriptors use `keepInitialState = false` in Phase 2 so NVRHI does not also auto-track them. The swapchain / app back buffer stays `keepInitialState = true`. Do not change that flag outside `FormatMap`.
- Do not move framebuffer creation or `setGraphicsState` into `GraphExecutor`. NVRHI binds the framebuffer and the pipeline in one `setGraphicsState`. `Pass::Execute` keeps that. Shaders, markers, and lambda bodies stay. Only the declarations around the lambdas change.
- Do not add `Compute` or `Copy` pass flags. No real pass needs them yet. DXR flags wait for S6.
- Do not edit `.github/workflows/windows.yml`. `RenderLabRenderer` stays free of RDG headers.

## Target API

```cpp
struct TextureHandle
{
    uint32_t index = kNullHandleIndex;
    uint32_t graphId = 0;
};

struct BufferHandle
{
    uint32_t index = kNullHandleIndex;
    uint32_t graphId = 0;
};

struct ResourceAccess
{
    ResourceKind kind = ResourceKind::Texture;
    uint32_t index = 0;
    Access access = Access::Unknown;
};

class PassBuilder
{
public:
    void Use(TextureHandle handle, Access access);
    void Use(BufferHandle handle, Access access);
};

class GraphBuilder
{
public:
    void SetInitialAccess(TextureHandle handle, Access access);
    void SetInitialAccess(BufferHandle handle, Access access);
    void ExportTexture(TextureHandle handle, Access finalAccess);
    void ExportBuffer(BufferHandle handle, Access finalAccess);
};
```

Delete `StaleVersion` and `SupersededUse`. Delete `ResourceVersionRecord`, `currentVersion`, and `DumpVersions`. A handle is foreign when `graphId` does not match, null when the index is null, and a type mismatch when the kind does not match. There is no stale-version check in `PassContext` or `GraphExecutor`.

`Use` rejects `Unknown`, `Present`, and any value that is not a single bit in `kKnownAccessBits`. `SetInitialAccess` and `Export*` accept `Present` as well as the pass-use bits, still one bit. `SetInitialAccess` on a `Create*` resource is `IncompatibleAccess`. A second `SetInitialAccess` or a second export final access that differs is `IncompatibleAccess`. `Plan()` on an import with no initial access is `UnknownAccess` and leaves the plan empty, same as today's unmapped-access failure.

Raster frame declarations, in both `BuildRasterFrameGraph` and `RenderingLabApp::ExecuteRasterFrame`:

- GBuffer: `GBufferA` / `B` / `C` as `RenderTarget`, `GBufferDepth` as `DepthWrite`.
- DeferredLighting and LightingDebug: GBuffer and depth as `ShaderResource`, `HDRSceneColor` as `RenderTarget`.
- PostProcess: `HDRSceneColor` as `ShaderResource`, back buffer as `RenderTarget`.
- GBufferDebug: GBuffer and depth as `ShaderResource`, back buffer as `RenderTarget`. No HDR chain.
- Back buffer: `SetInitialAccess(..., Access::Present)` and `ExportTexture(..., Access::Present)`.

Do not rewrite lambda bodies, snapshot writes, or pass `Execute` functions.

## Dump grammar

`CompileResult::Dump()` and `DumpDot()` drop version numbers, the `versions:` section, per-version lifetime lines, and the `cycle:` section.

Access lines use the access name, then kind, index, and quoted name:

```text
accesses:
  0 "GBuffer"
    RenderTarget texture 1 "GBufferA"
    DepthWrite texture 4 "GBufferDepth"
```

Edges are last-producer links. One edge per producer/consumer pair. Resource lines under that edge are sorted by resource index. Edge order is consumer index, then producer index.

```text
edges:
  0 "GBuffer" -> 1 "DeferredLighting"
    texture 1 "GBufferA"
    texture 4 "GBufferDepth"
```

`DumpDot()` uses the same edges and access names. No `vN` in labels. Update `tests/golden/rdg/rdg.txt` and `tests/golden/rdg/rdg.dot` to whatever `--dump-rdg` / the dump test prints after this grammar. Do not keep the old versioned golden.

## Phase 1 — identity, uses, order

CPU only. No `setTextureState`. No `FormatMap` change. Image goldens must not change; do not run them in this phase.

Touch the logical model and every caller of `Read` / `Write` / `.version` / `AccessMode` / `InferAccess` / `DumpVersions` / `StaleVersion` / `SupersededUse`:

- [`src/rdg/Handle.h`](src/rdg/Handle.h), [`src/rdg/Pass.h`](src/rdg/Pass.h), [`src/rdg/GraphBuilder.h`](src/rdg/GraphBuilder.h), [`src/rdg/GraphBuilder.cpp`](src/rdg/GraphBuilder.cpp)
- [`src/rdg/GraphCompiler.h`](src/rdg/GraphCompiler.h), [`src/rdg/GraphCompiler.cpp`](src/rdg/GraphCompiler.cpp)
- [`src/rdg/AccessPlan.h`](src/rdg/AccessPlan.h), [`src/rdg/AccessPlan.cpp`](src/rdg/AccessPlan.cpp), [`src/rdg/AccessMap.h`](src/rdg/AccessMap.h), [`src/rdg/AccessMap.cpp`](src/rdg/AccessMap.cpp)
- [`src/rdg/GraphExecutor.cpp`](src/rdg/GraphExecutor.cpp), [`src/rdg/PassContext.cpp`](src/rdg/PassContext.cpp)
- Factories: [`src/rdg/M1ShapedGraph.cpp`](src/rdg/M1ShapedGraph.cpp), [`src/rdg/ToneMapGraph.cpp`](src/rdg/ToneMapGraph.cpp), [`src/rdg/LightingPresentGraph.cpp`](src/rdg/LightingPresentGraph.cpp), [`src/rdg/RasterFrameGraph.cpp`](src/rdg/RasterFrameGraph.cpp)
- [`src/app/RenderingLabApp.cpp`](src/app/RenderingLabApp.cpp) declarations only
- RDG tests under `tests/test_rdg_*.cpp`, plus [`tests/golden/rdg/rdg.txt`](tests/golden/rdg/rdg.txt) and [`tests/golden/rdg/rdg.dot`](tests/golden/rdg/rdg.dot)

`Plan()` reads the declared `Access` as the pass's required state. Initial state is `SetInitialAccess` for imports and the `FormatMap` state for `Create*`. Final state for an exported resource is the export access. Final state otherwise equals initial. The before/after walk stays; it no longer calls `InferAccess`.

`GraphExecutor::Execute` still does not issue barriers in this phase.

Tests that asserted version identity, superseded handles, WAW keeping an overwritten pass, or `cycle:` need new assertions for the rules above. Keep the assertions that still apply: null handle, foreign graph, type mismatch, `ReadBeforeProduce`, duplicate use in one pass, culling of an unused leaf, lifetimes, reuse pairs, and the raster-frame live order (GBuffer, DeferredLighting, PostProcess for Final).

## Phase 2 — barriers

After Phase 1 is green.

`FormatMap::MakeTextureDesc` / `MakeBufferDesc`: `keepInitialState = false`. Initial state values stay (color render target, depth write, buffer shader resource).

`GraphExecutor::Execute`, when `Plan()` succeeded:

- Before each live lambda, for each use whose tracked state differs from the required access, record an `IssuedTransition` and, if `graphicsCommandList != nullptr`, call `setTextureState` or `setBufferState` on that resource's `native`, then one `commitBarriers` for the batch.
- After the last live pass, do the same for each exported resource whose state differs from its final access. Those log entries use `Error::kNoPass`.
- Tracked state starts at the plan's initial access. Update it as transitions are issued.
- A null command list still fills the log. A null `native` records the existing unbound/unregistered error and skips the NVRHI call.

Expose the log as `std::span<const IssuedTransition> GetIssuedTransitions() const` with resource name, pass index, `before`, and `after`. CPU tests in [`tests/test_rdg_access.cpp`](tests/test_rdg_access.cpp) or [`tests/test_rdg_raster.cpp`](tests/test_rdg_raster.cpp) assert the Final frame's transitions: GBuffer targets into `RenderTarget` / `DepthWrite` from their initial state only when the state actually changes, lighting reads as `ShaderResource`, HDR as `RenderTarget`, back buffer `Present` to `RenderTarget` then back to `Present`. Do not require a transition when before and after match.

`Pass::Execute` still clears and calls `setGraphicsState`. Those calls now run after the graph's barriers, with `keepInitialState` false on RDG-created textures.

## Docs

- Add `docs/adr/ADR-005-rdg-ue-resource-identity.md`. One identity per resource, `Use(handle, access)`, insertion order, last-producer edges, explicit initial/final access, barriers issued in `Execute`. State what it does not change: collected errors, public `Compile()`, cull reasons, dumps, S5.7 reuse rules, pass `Execute` owning `setGraphicsState`.
- Update the current-status sections of [`docs/rdg.md`](docs/rdg.md), [`README.md`](README.md), and the opening status blurb of [`IMPLEMENTATION_PLAN.md`](IMPLEMENTATION_PLAN.md). Do not rewrite old step bodies or older files under `docs/superpowers/plans/`.
- After both phases are green, add a completion block to [`docs/PROGRESS.md`](docs/PROGRESS.md). `Commit` may say `uncommitted working tree` until the user asks for a commit. Do not commit unless the user asks.

## Verification

Phase 1, required before Phase 2:

- `cmake --build out/build/windows-vs2022 --target RenderLabDataContractTests --config Debug`, then `out/build/windows-vs2022/bin/Debug/RenderLabDataContractTests.exe`. 0 failures.
- The same target and run for `Release`.
- `cmake --build out/build/windows-vs2022 --target RenderLab --config Debug`.

Phase 2, after the barrier change:

- Debug and Release `RenderLabDataContractTests` again, 0 failures, including the new transition log checks and the existing S5.6 / S5.7 checks.
- Debug `RenderLab` again.
- `powershell -NoProfile -File scripts/golden.ps1 -Mode Verify -Configuration Debug`
- `powershell -NoProfile -File scripts/golden-hdr.ps1 -Mode Verify -Configuration Debug`

Search under `src/` and `tests/`, expected empty:

- `AccessMode`, `InferAccess`, `StaleVersion`, `SupersededUse`, `DumpVersions`, `.version`

`src/rdg` still must not include `donut/`.

## Out of scope

- Collapsing `ExecuteRasterFrame` onto `BuildRasterFrameGraph`, or thinning lambdas.
- Moving snapshot fills out of lambdas.
- Shader-parameter structs, uniform buffers, blackboard, subresource states.
- `Compute` / `Copy` / async compute / render-pass merge / parallel setup.
- Creating the framebuffer or calling `setGraphicsState` inside `GraphExecutor`.
- A pool that outlives the executor. Reuse stays inside one `Allocate()`.
- Deleting CPU tests or changing the GitHub workflow.
- Changing S5.7 reuse matching or `--no-transient-reuse`.
