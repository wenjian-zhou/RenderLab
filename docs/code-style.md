# Code Style

Status: **active convention** (adopted 2026-09-08; existing code was aligned
to it in the same style pass)

This is the naming, enum, and formatting convention for RenderLab-owned code
(`src/`, `tests/`). Donut/NVRHI follow their own upstream conventions and are
never renamed. Renderer semantics (spaces, matrices, color) live in
[`renderer-conventions.md`](renderer-conventions.md); this file is only about
how the code itself is written.

## 1. Naming

| Element | Convention | Examples |
|---|---|---|
| Types (struct / class / enum) | PascalCase, no Hungarian prefix (no `F`/`T`/`E`/`I`) | `GBufferPass`, `GraphBuilder`, `PassFlags` |
| Functions and methods | PascalCase | `MakeViewConstants`, `CreateTexture`, `IsNull` |
| Locals and parameters | camelCase | `bytesPerElement`, `cameraPos` |
| Struct fields | camelCase | `passIndex`, `currentVersion` |
| Member variables | `m_` + camelCase | `m_graphId`, `m_passes` |
| File-scope globals / statics | `g_` + camelCase | `g_nextGraphId`, `g_failures` |
| Constants | `k` + PascalCase | `kNullHandleIndex`, `kViewOffsetFlags` |
| Namespaces | lowercase, nested | `renderlab`, `renderlab::rdg` |
| Files | PascalCase matching the primary type; tests are `test_` + snake_case | `GraphBuilder.h`, `test_rdg_handles.cpp` |

Namespace casing is a deliberate choice: `std`, `donut`, and `nvrhi` are all
lowercase, `renderlab` is already lowercase across the whole tree, and the
lowercase-namespace / PascalCase-type split makes the two readable apart.

### Method verb vocabulary

New methods pick their verb from the established set before inventing a new
one: `Make*` (pure factory), `Create*` (allocate and register), `Get*`
(accessor), `Is*` / `Has*` (predicate), `Build*` / `Convert*` / `Validate*`
(transform or check), `Run*` (test suite), `Check` (test assertion).

## 2. Enums

1. Always `enum class` with an explicit underlying type — `uint8_t` for
   compact sets, `uint32_t` for constant-buffer payloads and flags.
2. Enumerators are PascalCase with no prefix and never repeat the enum name:
   `AccessMode::Read` — not `ERdgAccess`, `ACCESS_READ`, or `AccessModeRead`.
3. Sentinel vocabulary: `None = 0` for an empty flag set; `Unknown` for an
   unset value; `Count` as the trailing array bound (`GBufferTarget::Count`).
4. Flag enums: collection-noun name, free `constexpr operator|`,
   `operator|=`, and `operator&`, plus a `kKnown*` validation mask
   (`PassFlags`, `kKnownPassFlagBits`). Compose flags in enum space and
   `static_cast` to the payload integer once, at the constant-buffer
   boundary.
5. HLSL-shared headers (`src/shaders/*_cb.h`, `*.hlsli`) use the
   **two-layer pattern**: shared `static const uint Xxx_Value = n` constants
   (HLSL has no scoped enums, so the prefix is required there), plus an
   `#ifdef __cplusplus` `enum class Xxx` aliasing them. C++ code uses only
   the scoped names; HLSL keeps the prefixed constants. Bits are identical.
   Precedent: `gbuffer_debug_cb.h`, `renderer_cb.h`.
6. The existing `kGBufferFlag*` constants are C++-side packing bits
   (grandfathered); new flag sets use `enum class`.

## 3. Formatting

- C++20, 4-space indent, Allman braces for functions and types.
- `#pragma once`; NSDMI defaults (`= 0`, `= nullptr`); defaulted comparison
  friends (`friend bool operator==(...) = default`).
- Constructor initializer lists on their own line, `: member(value)` style.
- `std::format` for composed runtime strings (rdg error messages); `printf`
  in test output (existing convention).
- No `[[nodiscard]]` (unused repo-wide; keep it that way). `auto` only for
  obvious locals; interfaces use explicit types.

## 4. Comments

- English. State constraints the code cannot show (why `version` lives in
  the handle), never narrate the next line.
- A decision with a rationale belongs in `docs/` (e.g. [`rdg.md`](rdg.md),
  an ADR) or [`ue-rdg-survey.md`](ue-rdg-survey.md) and is referenced from
  code, not restated.
- Do not leave review-notes in comments ("changed from X to Y"); history
  belongs to git.

## 5. Conventions deliberately rejected

- UE Hungarian type prefixes (`F`, `T`, `E`, `I`): RenderLab adopts UE
  semantics where the surveys justify them, not UE naming.
- PascalCase namespaces (a .NET/Java habit; see §1).
- ALL_CAPS enumerators (C macro style).
- Lowercase `get`-prefixed accessors (Java style).

## 6. Enforcement

There is no automated formatter or linter configured; conformance is
review-enforced. When a rule here conflicts with a file that predates this
document, follow the document for new code and align the old file only when
it is already being modified for other reasons — except shared-header enum
and flag naming, which was aligned wholesale in the 2026-09-08 style pass.
