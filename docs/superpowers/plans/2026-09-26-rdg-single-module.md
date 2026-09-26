# RDG Single Module

Status: ready to execute. Do not expand this into the call-site thinning discussed earlier (factory-only declarations, snapshot query, one-line lambdas). That is a later change. This plan only removes the `RenderLabRdg` / `RenderLabRdgExec` split and puts the pass lambda on `GraphBuilder::AddPass`.

## Decisions already made

- Supersede only the zero-NVRHI library boundary in ADR-003. Keep versioned handles, explicit `Read` / `Write`, collected `rdg::Error`s, and the neutral `rdg::Format` / `rdg::Access` enums.
- One CMake target, `RenderLab::Rdg`, links `RenderLab::ProjectOptions` and `nvrhi`. It does not link donut.
- Delete target `RenderLabRdgExec` / `RenderLab::RdgExec`.
- Namespace `renderlab::rdg::exec` goes away. Moved types live in `renderlab::rdg`.
- `GraphBuilder::AddPass` gains an overload that takes the pass lambda. The existing overload without a lambda stays, for compile-only tests and `BuildRasterFrameGraph`.
- The lambda is not a field of `PassRecord`. `PassRecord` stays name, flags, and accesses. The builder stores `std::function` in a side table keyed by pass index, which is what `ExecGraph` does today.
- CPU tests stay. Do not edit `.github/workflows/windows.yml`. `RenderLabDataContractTests` still runs in Debug and Release with no GPU device. `Allocate()` without `SetDevice` still uses CPU stubs.
- `RenderLabRenderer` stays free of RDG headers. Pass `Execute` implementations, shaders, and markers do not change.
- `Compile()` stays a public CPU step. `GraphExecutor::Execute(ICommandList*)` stays the only public run entry. `SelectCommandList` stays the queue seam and still returns the graphics list. S5.7 transient reuse behavior does not change.

## Target API

```cpp
namespace renderlab::rdg
{
    class ICommandList; // forward-declared from nvrhi; do not include nvrhi in GraphBuilder.h

    using PassLambda = std::function<void(nvrhi::ICommandList*, PassContext&)>;

    class GraphBuilder
    {
    public:
        PassBuilder AddPass(std::string_view name, PassFlags flags);
        PassBuilder AddPass(std::string_view name, PassFlags flags, PassLambda lambda);

        const PassLambda* FindLambda(uint32_t passIndex) const;
    };
}
```

`AddPass` with a lambda matches today's `ExecGraph::AddPass`: call the no-lambda `AddPass`, and store the function only when the pass was actually added (`IsValid()` and the pass count grew). A rejected declaration does not keep the lambda.

`FindLambda` returns null when the index is out of range or the function is empty. `GraphExecutor::Execute` keeps today's rules: a live pass with no lambda records `InvalidPass` and stops later passes; a culled pass is not invoked and is not `InvalidPass`; lookup errors inside a lambda do not stop the walk.

`GraphExecutor` has one constructor, `(const GraphBuilder&, const CompileResult&)`. It reads lambdas from that builder. The builder must outlive the executor. Delete the `ExecGraph` constructor and `m_graph`.

Delete class `ExecGraph`. Production and tests that built an `ExecGraph` build a `GraphBuilder` and call the lambda overload.

## File move

Move these from `src/rdg/exec/` to `src/rdg/` and switch their namespace from `renderlab::rdg::exec` to `renderlab::rdg`:

- `PhysicalResource.h`
- `PhysicalRegistry.h` / `.cpp`
- `PassContext.h` / `.cpp`
- `GraphExecutor.h` / `.cpp`
- `FormatMap.h` / `.cpp`
- `AccessMap.h` / `.cpp`
- `AccessPlan.h` / `.cpp`
- `TransientReuse.h` / `.cpp`

Delete `src/rdg/exec/ExecGraph.h` and `ExecGraph.cpp` after their lambda table lives on `GraphBuilder`.

Includes become `rdg/GraphExecutor.h`, `rdg/PassContext.h`, and so on. No `rdg/exec/` includes remain under `src/` or `tests/`.

`GraphBuilder.h` forward-declares `nvrhi::ICommandList` and includes `PassContext.h`. It does not include an NVRHI header. `FormatMap` and `GraphExecutor.cpp` continue to include NVRHI, and the CMake target links `nvrhi`.

## CMake

In `src/CMakeLists.txt`:

- Append the moved sources to `RENDERLAB_RDG_SOURCES`.
- Delete `RENDERLAB_RDG_EXEC_SOURCES`, `RenderLabRdgExec`, and the `RenderLab::RdgExec` alias.
- `RenderLabRdg` links `PUBLIC RenderLab::ProjectOptions` and `nvrhi`.
- `RenderLab` and `tests/CMakeLists.txt` drop `RenderLab::RdgExec`. They already link `RenderLab::Rdg`.

## Call sites

`src/app/RenderingLabApp.h` holds `std::unique_ptr<rdg::GraphBuilder> m_frameGraph` and `std::unique_ptr<rdg::GraphExecutor> m_frameExecutor`. Declare the graph member first so the executor is destroyed first.

`ExecuteRasterFrame` constructs a `GraphBuilder`, calls `AddPass(name, flags, lambda)`, then `Compile`, `GraphExecutor(builder, compiled)`, `SetDevice`, `RegisterImport`, `Allocate`, `Plan`, `Execute`. Do not rewrite the lambda bodies, the snapshot writes, or the manual `Read` / `Write` lists in this change.

Tests that use `ExecGraph` (`test_rdg_exec.cpp`, `test_rdg_alloc.cpp`, `test_rdg_access.cpp`, `test_rdg_tonemap.cpp`, `test_rdg_lighting.cpp`, `test_rdg_raster.cpp`, `test_rdg_reuse.cpp`) switch to `GraphBuilder` plus the lambda overload. Keep their assertions. Compile-only tests stay on the no-lambda `AddPass`.

`BuildRasterFrameGraph`, `BuildLightingPresentGraph`, `BuildToneMapGraph`, and `BuildM1ShapedGraph` stay no-lambda declaration factories.

## Docs

- Add `docs/adr/ADR-004-rdg-single-module.md`. State that it supersedes ADR-003's split into a NVRHI-free compiler target and an execution target. List what it does not supersede: versioned handles, explicit read/write, collected errors, neutral format and access enums, public `Compile()`, device-free CPU tests.
- At the top of `docs/adr/ADR-003-rdg-boundary.md`, add a short banner that the module split is superseded by ADR-004. Leave the historical S4–S5.7 sections in place.
- Update the current-status sections of `docs/rdg.md`, `README.md`, and the opening status blurb of `IMPLEMENTATION_PLAN.md` so they describe one `RenderLab::Rdg` target that links `nvrhi`, and `GraphBuilder::AddPass` with an optional lambda. Do not rewrite old step bodies or `docs/superpowers/plans/` history.
- After Debug and Release tests are green, add a completion block to `docs/PROGRESS.md`. `Commit` may say `uncommitted working tree` until the user asks for a commit. Do not commit unless the user asks.

## Verification

Required:

- `cmake --build out/build/windows-vs2022 --target RenderLabDataContractTests --config Debug`, then run `out/build/windows-vs2022/bin/Debug/RenderLabDataContractTests.exe`. 0 failures, including the existing S5.6 raster-frame and S5.7 reuse checks.
- The same target and run for `Release`.
- `cmake --build out/build/windows-vs2022 --target RenderLab --config Debug`.

Search gates, expected empty under `src/` and `tests/`:

- `RenderLabRdgExec`, `RenderLab::RdgExec`, `rdg::exec`, `rdg/exec/`, `ExecGraph`

`src/rdg` still must not include `donut/`.

GPU goldens are not required. This change does not alter pass order, allocations, or shaders. Do not drop the CPU test steps from CI.

## Out of scope

- Collapsing `ExecuteRasterFrame` onto `BuildRasterFrameGraph`.
- Moving snapshot fills out of lambdas.
- Shader-parameter structs, automatic render-pass setup, or issuing `Plan()` as GPU barriers.
- Deleting CPU tests or changing the GitHub workflow.
- Changing S5.7 reuse rules, `--no-transient-reuse`, or culling (`ZeroUseAllocation` stays).
- Putting `Pass::Execute` into the logical pass record.
