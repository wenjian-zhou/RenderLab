# S5.1 Imported Resources and Execution Context

> **For agentic workers:** S5.1 is implemented. Use this file as the locked-decision record, not as a queue of unfinished tasks.

**Goal:** Bind Stage 4 logical imported handles to opaque physical tokens and give synthetic pass callbacks a `PassContext` that resolves only that pass's declared handles — with no GPU device, no NVRHI, and no renderer changes.

**Architecture:** Target `RenderLabRdgExec` (`src/rdg/exec/`, namespace `renderlab::rdg::exec`) links only `RenderLab::Rdg`. `GraphBuilder` stays logical. `GraphExecutor` binds imports, then `ExecutePass` activates a per-pass `PassContext` whose `GetTexture`/`GetBuffer` return a non-owning pointer or `nullptr` plus a collected `rdg::Error`.

**Tech Stack:** C++20, CMake `windows-vs2022`, `Check()` harness in `RenderLabDataContractTests`.

---

## Locked grill decisions

1. **Module:** `src/rdg/exec/` + `RenderLabRdgExec` / `RenderLab::RdgExec`, namespace `renderlab::rdg::exec`. S5.1 links only `RenderLab::Rdg`. S5.2 adds NVRHI to this same target.
2. **Physical identity:** `PhysicalTexture` / `PhysicalBuffer` are `{void* native; std::string debugName;}`. Non-owning. No extra generation.
3. **Import bind:** Two-phase. Logical `Import*` unchanged. `RegisterImport(handle, physical)` on the executor. Physical slot is per resource index. Any in-range version of an imported resource is accepted.
4. **Context:** Non-copyable `PassContext` with `GetTexture` / `GetBuffer` returning `const PhysicalTexture*` / `const PhysicalBuffer*`. Failure: `nullptr` + `rdg::Error`. No registry/compile/command-list API on the context.
5. **Validity:** Live only during that pass's `ExecutePass` callback. Lookup requires the resource and the exact declared version. After the callback, `ExpiredContext`.
6. **Execute:** No `AddPass` lambda. `GraphExecutor::ExecutePass(passIndex, callback)`. Culled/OOB/failed compile: `InvalidPass`, no callback.
7. **Export:** `GetExported(handle)` returns the registered native. Allowed as soon as the import is registered. No extract-copy.
8. **Errors:** Extend `rdg::ErrorCategory` (`UndeclaredAccess`, `ExpiredContext`, `UnregisteredImport`, `InvalidPass`). Same collected-error model in Debug and Release. Duplicate bind and RegisterImport on internals: `IncompatibleAccess`.
9. **Tests:** `tests/test_rdg_exec.cpp`. RegisterImport is imported-only. M1 binds only BackBuffer.
10. **Docs:** `docs/rdg.md`, ADR-003 S5.1 extension, README, IMPLEMENTATION_PLAN, PROGRESS. Do not rewrite `m1-reference.md`.

## Not in S5.1

- Allocate internals or map `rdg::Format` to device formats (S5.2)
- Bind dummy natives onto `CreateTexture` slots
- Access-state / barrier planning (S5.3)
- Execute lambdas on `AddPass`
- Command lists, NVRHI includes, Donut includes, PIX, device creation
- Migrate PostProcess / Deferred / GBuffer (S5.4–S5.6)
- Transient reuse (S5.7)

## Files

Created under `src/rdg/exec/`: `PhysicalResource.h`, `PhysicalRegistry.h/.cpp`, `PassContext.h/.cpp`, `GraphExecutor.h/.cpp`. Tests: `tests/test_rdg_exec.cpp`.
