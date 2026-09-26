# ADR-004: RDG Is One Module

- Status: Accepted
- Date: 2026-09-26
- Supersedes: the NVRHI-free compiler target vs execution target split in
  [ADR-003](ADR-003-rdg-boundary.md)
- Related: [`../rdg.md`](../rdg.md),
  [`../../IMPLEMENTATION_PLAN.md`](../../IMPLEMENTATION_PLAN.md)

## Decision

`RenderLab::Rdg` is one CMake target. It links `RenderLab::ProjectOptions`
and `nvrhi`. It does not link donut. The target `RenderLabRdgExec` /
`RenderLab::RdgExec` and the namespace `renderlab::rdg::exec` are gone.
Moved execution types live in `renderlab::rdg`.

`GraphBuilder::AddPass` has an overload that takes the pass lambda. The
overload without a lambda stays, for compile-only tests and the declaration
factories. The lambda is not a field of `PassRecord`. `PassRecord` stays
name, flags, and accesses. The builder stores `std::function` in a side
table keyed by pass index. `GraphBuilder.h` forward-declares
`nvrhi::ICommandList` and does not include an NVRHI header.

`Compile()` stays a public CPU step. `GraphExecutor::Execute(ICommandList*)`
stays the only public run entry. The builder must outlive the executor.

## What this does not supersede

ADR-003 still stands for:

- Versioned handles (`index`, `version`, `graphId`).
- Explicit `Read` / `Write` on pass records.
- Collected `rdg::Error` values.
- Neutral `rdg::Format` and `rdg::Access` enums.
- Public `Compile()`, and device-free CPU tests. `Allocate()` without
  `SetDevice` still uses CPU stubs. `RenderLabDataContractTests` still runs
  with no GPU device.

## Context

ADR-003 split the logical compiler from execution so Stage 4 could be tested
without NVRHI. By S5.7 the app, the tests, and the executor already shared
one graph, and the second target only existed to keep `nvrhi` off
`GraphBuilder`. Putting the lambda on `GraphBuilder::AddPass` removes that
split without putting NVRHI types into `PassRecord` or into the compiler's
public header.
