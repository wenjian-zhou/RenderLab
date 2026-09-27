# Short AddPass Call Sites

Status: ready to execute. Start only after Phase 1 of [2026-09-26-rdg-ue-alignment.md](2026-09-26-rdg-ue-alignment.md) is green (`Use(handle, access)`, no handle `version`, `BuildRasterFrameGraph` already declares the frame). This plan does not implement Phase 2 barriers. If Phase 2 has already landed, leave it in place.

UE's tonemap pass (`PostProcessTonemap.cpp`, `AddDrawScreenPass` around line 1120) fills a parameter struct, then the lambda is two `SetShaderParameters` calls. The length sits above the lambda. This plan gives `ExecuteRasterFrame` that shape: the factory owns the declarations, each lambda is one call, and the frame snapshot is taken after `Execute`.

## Decisions already made

- `BuildRasterFrameGraph` stays a no-lambda declaration factory. CPU tests keep calling it and executing without lambdas (`InvalidPass` for a live pass with no lambda).
- Production does not restate those declarations. `ExecuteRasterFrame` calls the factory, then `GraphBuilder::SetLambda` on the returned pass indices.
- Each lambda at that call site is one statement: a private `RenderingLabApp` method. The method holds today's resolve-and-`Execute` glue. `Pass::Execute` signatures do not change. `RenderLabRenderer` stays free of RDG headers.
- Snapshot pointers are not written inside the lambdas. After `Execute`, the app copies `ITexture*` from `GraphExecutor::FindTexture`. `valid` is set only if the GBuffer method reached `GBufferPass::Execute`. `hasHdr` is set only if the lighting method reached `DeferredLightingPass::Execute`. Allocation alone does not make the snapshot valid: a `Create*` texture exists after `Allocate()` even when the pass did not run.
- `m_stopRasterFrame` stays. A failed resolve sets it and skips that pass's `Execute`. Later methods return immediately when the flag is set, so later snapshot bits stay clear.
- `PassContext::GetTexture` / `GetBuffer` stay pass-scoped and still reject undeclared handles. `FindTexture` is a post-`Allocate` registry lookup for the app. Pass code does not call it.
- Delete `RasterFrameHandles` / `m_frameHandles` when the lambdas no longer use them. The factory result is the handle list.
- Do not thin `BuildToneMapGraph` or `BuildLightingPresentGraph`. They stay CPU-test factories.
- Do not edit `.github/workflows/windows.yml`. Do not commit unless the user asks.

## Target API

```cpp
class GraphBuilder
{
public:
    // Stores the lambda when passIndex was added. An empty function clears
    // the slot. An out-of-range index records InvalidPass and does not store.
    void SetLambda(uint32_t passIndex, PassLambda lambda);
};

class GraphExecutor
{
public:
    // Physical token after Allocate(), including Create* resources.
    // Null when the handle is null, foreign, unknown, not allocated, or an
    // import that was not registered. Does not check pass declarations.
    const PhysicalTexture* FindTexture(TextureHandle handle) const;
    const PhysicalBuffer* FindBuffer(BufferHandle handle) const;
};
```

`ExecuteRasterFrame` after the factory returns:

```cpp
rdg::RasterFrameGraph shape =
    rdg::BuildRasterFrameGraph(*graph, width, height, present);
graph->SetLambda(shape.gbufferPassIndex,
    [this, shape](nvrhi::ICommandList* commandList, rdg::PassContext& ctx) {
        RunRasterGBuffer(commandList, ctx, shape);
    });
if (shape.hasHdr)
{
    graph->SetLambda(shape.deferredLightingPassIndex,
        [this, shape](nvrhi::ICommandList* commandList, rdg::PassContext& ctx) {
            RunRasterLighting(commandList, ctx, shape);
        });
}
graph->SetLambda(shape.presentPassIndex,
    [this, shape](nvrhi::ICommandList* commandList, rdg::PassContext& ctx) {
        RunRasterPresent(commandList, ctx, shape);
    });
```

`RunRasterGBuffer` / `RunRasterLighting` / `RunRasterPresent` are private. They keep the current null checks, `m_stopRasterFrame`, input/output assembly, and the existing `Pass::Execute` calls. They do not assign `m_frameSnapshot`. On the success path they set `m_rasterGBufferRan` or `m_rasterLightingRan`.

`CaptureRasterSnapshot(const rdg::RasterFrameGraph&)` runs after `Execute`:

- Always clear the snapshot first (already done at the start of the frame).
- If `m_rasterGBufferRan`, copy GBuffer and depth through `FindTexture` + `NativeTexture`, and set `width`, `height`, and `valid` when all four pointers are non-null.
- If `m_rasterLightingRan`, copy HDR and set `hasHdr` when that pointer is non-null.
- GBuffer-debug leaves `hasHdr` false. A lighting failure after a successful GBuffer leaves `valid` true and `hasHdr` false, matching today's lambdas.

HDR uses the handle the present pass reads (`hdrWritten` if the factory still has both fields, otherwise the single HDR handle). Do not treat `hdrCreated` and `hdrWritten` as two resources when Phase 1 has made them the same identity.

## Files

- [`src/rdg/GraphBuilder.h`](src/rdg/GraphBuilder.h), [`src/rdg/GraphBuilder.cpp`](src/rdg/GraphBuilder.cpp): `SetLambda`.
- [`src/rdg/GraphExecutor.h`](src/rdg/GraphExecutor.h), [`src/rdg/GraphExecutor.cpp`](src/rdg/GraphExecutor.cpp): `FindTexture` / `FindBuffer`.
- [`src/app/RenderingLabApp.h`](src/app/RenderingLabApp.h), [`src/app/RenderingLabApp.cpp`](src/app/RenderingLabApp.cpp): factory + `SetLambda` + private run methods + snapshot capture. Remove `m_frameHandles` if unused.
- [`tests/test_rdg_raster.cpp`](tests/test_rdg_raster.cpp): the cases below. Keep the existing declaration, cull, allocate, and execute assertions.

## Tests

- `SetLambda` on a real pass runs during `Execute`. `SetLambda` with an empty function leaves that live pass `InvalidPass`. An out-of-range index records `InvalidPass` and does not affect later passes.
- `BuildRasterFrameGraph` without `SetLambda` still fails `Execute` with `InvalidPass`.
- After `Allocate()` and no device, `FindTexture` on a `Create*` handle returns the CPU stub. `FindTexture` on a null handle, a foreign graph, and an unregistered import returns null. `FindTexture` does not require the caller to be inside a pass.

## Docs

Update the current production-frame paragraph in [`docs/rdg.md`](docs/rdg.md): the app calls `BuildRasterFrameGraph`, attaches lambdas with `SetLambda`, and fills `RasterFrameSnapshot` from `FindTexture` after `Execute`. One sentence in the opening status blurbs of [`README.md`](README.md) and [`IMPLEMENTATION_PLAN.md`](IMPLEMENTATION_PLAN.md). After tests are green, add a completion block to [`docs/PROGRESS.md`](docs/PROGRESS.md). `Commit` may say `uncommitted working tree` until the user asks. Do not rewrite old step bodies.

## Verification

- `cmake --build out/build/windows-vs2022 --target RenderLabDataContractTests --config Debug`, then `out/build/windows-vs2022/bin/Debug/RenderLabDataContractTests.exe`. 0 failures, including S5.6 and S5.7.
- The same target and run for `Release`.
- `cmake --build out/build/windows-vs2022 --target RenderLab --config Debug`.
- `powershell -NoProfile -File scripts/golden.ps1 -Mode Verify -Configuration Debug`
- `powershell -NoProfile -File scripts/golden-hdr.ps1 -Mode Verify -Configuration Debug`

`src/rdg` still must not include `donut/`. `src/renderer` still must not include `rdg/`.

## Out of scope

- Phase 2 barriers, `keepInitialState`, and `IssuedTransition`.
- Shader-parameter macros, generated dispatch, or changing `Pass::Execute`.
- Moving framebuffer creation or `setGraphicsState`.
- Thinning the tone-map or lighting-present factories.
- A cross-frame pool, `Compute` / `Copy` flags, or DXR.
- Deleting CPU tests or changing the GitHub workflow.
