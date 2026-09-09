# RDG S4.1 Data Flow Walkthrough

Status: **reference walkthrough** — diagrams pinned to the code at commit
`35846e2`. This is a historical S4.1 snapshot, not the current S4.2 field or
method map. S4.2 adds version records, returning Write calls, access validation,
and a provenance dump; the current API contract is in [`rdg.md`](rdg.md).

This is the picture behind [`rdg.md`](rdg.md): who owns what, how one
declaration travels through validation into storage, and which method touches
which member. Every arrow below can be checked against a `file:line` in §6.
The model itself — semantics, failure taxonomy, boundary — is defined in
[`rdg.md`](rdg.md); this file only draws it.

## 1. Ownership and identity

```mermaid
flowchart TB
    subgraph values["Handle value types - public fields, copyable, forgeable by design (Handle.h:22,33)"]
        TH["TextureHandle / BufferHandle<br/>index - registry slot, null = 0xFFFFFFFF (Handle.h:7)<br/>version - the S4.2 version slot, always 0 in S4.1<br/>graphId - owning graph identity"]
    end

    subgraph builder["GraphBuilder - one per graph, stack-scoped, non-copyable (GraphBuilder.h:84)"]
        direction LR
        GID["m_graphId<br/>minted from g_nextGraphId (GraphBuilder.cpp:13,67)<br/>never 0, so a default-constructed handle can never resolve"]
        RES[("m_resources - append-only (GraphBuilder.cpp:92,104)<br/>ResourceRecord (GraphBuilder.h:44):<br/>name · kind · desc variant<br/>imported · exported · currentVersion = 0")]
        PAS[("m_passes - append-only (GraphBuilder.cpp:192)<br/>PassRecord (Pass.h:52): name · flags<br/>accesses: ResourceAccess (Pass.h:44)<br/>= kind · index · version · mode")]
        ERR[("m_errors - sole writer is AddError (GraphBuilder.cpp:378)<br/>Error (GraphBuilder.h:31): category · message<br/>passIndex · passName · resourceName")]
    end

    TH -.->|"index selects a slot"| RES
    TH -.->|"version must equal currentVersion"| RES
    TH -.->|"graphId must equal m_graphId"| GID
```

Three things this picture fixes:

- **A handle is not a pointer.** It is three `uint32`s that only mean
  something when checked against the owning builder's stores — that is why
  validation, not encapsulation, is the safety net.
- **The two record stores are append-only.** Indices are never recycled
  within a graph, so `version` is the only staleness axis — the precondition
  S4.2's write-mints-new-handle semantics depends on.
- **`ResourceAccess.version` is the handle's version pinned at declaration
  time**, not a live view — the evidence trail S4.2 needs to reject use of a
  superseded version.

## 2. The declaration flow (Read / Write)

The core path. Every `Read`/`Write` on a `PassBuilder` funnels into the same
five-step waterfall; the order is a contract (earliest category wins), not an
implementation detail.

```mermaid
flowchart LR
    U["caller: pass.Read(h) / pass.Write(h)"] --> PB["PassBuilder (GraphBuilder.h:58)<br/>( GraphBuilder · passIndex ) - a value, never a pointer into storage<br/>invalid builder = no-op"]
    PB --> DAI["DeclareAccessInternal (GraphBuilder.cpp:232)"]
    DAI --> R1{"ResolveHandle step 1<br/>index == kNullHandleIndex"}
    R1 -->|"NullHandle"| AE["AddError (GraphBuilder.cpp:378)<br/>the only writer of m_errors<br/>the declaration is rejected - not recorded"]
    R1 -->|pass| R2{"step 2<br/>graphId != m_graphId"}
    R2 -->|"ForeignGraph"| AE
    R2 -->|pass| R3{"step 3<br/>index >= m_resources.size()"}
    R3 -->|"StaleVersion"| AE
    R3 -->|pass| R4{"step 4<br/>kind does not match the slot"}
    R4 -->|"TypeMismatch"| AE
    R4 -->|pass| R5{"step 5<br/>version != currentVersion"}
    R5 -->|"StaleVersion"| AE
    R5 -->|"all five pass"| ACC["m_passes[passIndex].accesses.push_back(<br/>ResourceAccess: kind · index · version · mode)"]
    AE --> ERR[("m_errors")]
    classDef reject fill:#fee,stroke:#c33;
    class AE,ERR reject;
```

Two properties visible here:

- **Rejection is per-declaration.** A bad handle rejects exactly that one
  declaration; the surrounding graph stays explorable (the
  per-declaration-rejection test asserts this).
- **Out-of-range and version-mismatch share `StaleVersion`** by design: with
  a correct `graphId` an out-of-range index can only be forged, which is the
  same "does not resolve to the current slot" condition.

## 3. The other three flows

```mermaid
flowchart LR
    AE["AddError -&gt; m_errors<br/>a rejected call leaves the graph unchanged by that call"]

    subgraph mint["Create* / Import* - minting a resource"]
        direction LR
        D["TextureDesc / BufferDesc"] --> V["ValidateTextureDesc / ValidateBufferDesc (GraphBuilder.cpp:116,156)<br/>name · extent · format · element fields -<br/>each violation is recorded separately"]
        V -->|"InvalidName / InvalidDescriptor"| AE
        V -->|"all checks pass"| CR["CreateTextureResource / CreateBufferResource (GraphBuilder.cpp:92,104)<br/>m_resources.push_back(ResourceRecord, currentVersion = 0)<br/>imported = true for Import*"]
        CR --> H["handle ( index · 0 · m_graphId )"]
    end

    subgraph addpass["AddPass - minting a pass"]
        direction LR
        N["name + flags"] --> FV{"empty name?<br/>unknown flag bits? (GraphBuilder.cpp:192)"}
        FV -->|"InvalidName / InvalidPassFlags"| AE
        FV -->|"valid"| PR["m_passes.push_back(PassRecord: name · flags · accesses[])"]
        PR --> PB2["PassBuilder( this · passIndex )<br/>a rejected AddPass returns an invalid one whose<br/>Read / Write are no-ops"]
    end

    subgraph exportf["Export* - marking an output"]
        direction LR
        EH["handle"] --> RH2{"ResolveHandle - the same<br/>five steps, operation =<br/>export declaration (GraphBuilder.cpp:260)"}
        RH2 -->|"any step fails"| AE
        RH2 -->|"valid"| EF["m_resources[index].exported = true (idempotent)<br/>imported || exported = cull root for S4.4"]
    end
```

All four entry surfaces (`Create*`, `AddPass`, `Read`/`Write`, `Export*`)
share one error sink and one rule: validate immediately, reject the single
call, keep collecting.

## 4. One complete construction, end to end

The M1-shaped graph from `tests/test_rdg_declarations.cpp` as a timeline:

```mermaid
sequenceDiagram
    autonumber
    participant T as test (M1-shaped graph)
    participant B as GraphBuilder
    participant R as m_resources
    participant P as m_passes

    T->>B: ImportTexture("BackBuffer")
    B->>R: push_back(ResourceRecord, imported = true)
    B-->>T: handle(0, 0, g)
    T->>B: CreateTexture x5 (GBufferA/B/C/Depth, HDRSceneColor)
    B->>R: push_back x5
    B-->>T: handles(1..5, 0, g)
    T->>B: AddPass("GBuffer", Raster)
    B->>P: push_back(PassRecord)
    B-->>T: PassBuilder(g, 0)
    T->>B: Write(gbufferA..depth) - four declarations
    Note over B: each one passes the five-step ResolveHandle
    B->>P: accesses += 4 x ResourceAccess
    T->>B: AddPass("DeferredLighting") + Read x4 + Write(hdr)
    T->>B: AddPass("PostProcess") + Read(hdr) + Write(backBuffer)
    T->>B: ExportTexture(backBuffer)
    B->>R: slot 0 . exported = true
    T->>B: GetErrors()
    B-->>T: empty - zero errors, 6 resources, 3 passes
```

## 5. Field access matrix

Direct member access per method (asterisk = routed through `AddError`, the
only direct writer of `m_errors`). `R` = read, `W` = write.

### GraphBuilder

| Method (`GraphBuilder.cpp`) | `m_graphId` | `m_resources` | `m_passes` | `m_errors` |
|---|---|---|---|---|
| constructor (:67) | **W** — minted from `g_nextGraphId` | | | |
| `GetGraphId` (GraphBuilder.h:92) | R | | | |
| `CreateTextureResource` / `CreateBufferResource` (:92, :104) | R — minted into the handle | **W** — push_back | | |
| `ValidateTextureDesc` / `ValidateBufferDesc` (:116, :156) | | | | W* |
| `AddPass` (:192) | | | **W** — push_back | W* |
| `ResolveHandle` (:270) | R | R — size + entry | | W* |
| `DeclareAccessInternal` (:232) | | (R — via ResolveHandle) | **RW** — reads pass name, appends access | W* |
| `ExportResource` (:260) | (R — via ResolveHandle) | **RW** — R via ResolveHandle, writes `exported` | | W* |
| `AddError` (:378) | | | | **W** — sole direct writer |
| `GetErrors` / `AssertNoErrors` (h:104, :361) | | | | R |
| `GetPassCount` / `GetPass` (h:107, :366) | | | R | |
| `GetResourceCount` / `GetResource` (h:109, :372) | | R | | |

### PassBuilder

| Method | `m_builder` | `m_passIndex` |
|---|---|---|
| `IsValid` (GraphBuilder.h:61) | R | |
| `PassIndex` (GraphBuilder.h:62) | | R |
| `Read` / `Write` (four overloads, GraphBuilder.cpp:35–64) | R — forwards to `DeclareAccess` | R |

The remaining types are plain data with at most one accessor: `IsNull` reads
`index` (Handle.h:28,39); descriptors, records, and `Error` have no methods.

## 6. Invariants the diagrams make visible

1. **Append-only stores** — indices never recycle within a graph, so version
   is the only staleness axis (S4.2's precondition).
2. **`ResolveHandle` is the single validation gate** — Read, Write, and
   Export share one five-step chain, so the failure taxonomy is identical at
   every entry point.
3. **`AddError` is the only writer of `m_errors`** — errors are collected
   data, never control flow.
4. **Rejection is per-call** — one bad declaration never poisons the
   surrounding graph.
5. **`PassBuilder` holds (builder, index)** — never a pointer into storage
   that can reallocate when another pass is appended.

## 7. Source index

| Diagram element | Source |
|---|---|
| Handle fields, null sentinel | `src/rdg/Handle.h:7,22–41` |
| Neutral `Format`, descriptors, `operator==` | `src/rdg/ResourceDesc.h` |
| `PassFlags`, `AccessMode`, `ResourceAccess`, `PassRecord` | `src/rdg/Pass.h` |
| `ErrorCategory`, `Error`, `ResourceRecord`, `PassBuilder`, `GraphBuilder` | `src/rdg/GraphBuilder.h` |
| Graph-id counter, validation chain, error construction | `src/rdg/GraphBuilder.cpp` |
| Failure-matrix and M1-shaped-graph tests | `tests/test_rdg_handles.cpp`, `tests/test_rdg_declarations.cpp` |
