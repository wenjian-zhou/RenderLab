# M1 Reference: The Frozen Manual Pipeline

Status: **frozen (S3.3, 2026-09-07)** — the RDG migration oracle for Stage 5

This document records what the M1 manual raster pipeline is: the reference
outputs that constitute the oracle, the pass execution order, the persistent
resources and their NVRHI states, and how to re-verify all of it. Stage 5
(S5.4–S5.6) migrates this pipeline onto the RDG and must reproduce these
outputs within the existing golden tolerances. This file records the current
state; it does not design RDG.

The M1 commit is git tag `m1`; the hash and evidence are in
[`PROGRESS.md`](PROGRESS.md). Conventions (handedness, matrices, reversed-Z,
color space) live in [`renderer-conventions.md`](renderer-conventions.md); the
GBuffer encoding in [`g-buffer.md`](g-buffer.md); the lighting contract in
[`lighting.md`](lighting.md); the tone curve in [`postprocess.md`](postprocess.md).

## 1. Reference set (the oracle)

The committed goldens **are** the M1 reference; nothing new was minted for the
freeze (S3.2 re-baselined the HDR golden days before the freeze and is fresh).

| Layer | Files | Contract |
|---|---|---|
| GBuffer PNGs | `tests/golden/cesium-milk-truck/s04-default/1280x720/` — six `gbuffer-*.png` views + `capture-metadata.json` | [`image-regression.md`](image-regression.md) |
| Pre-exposure HDR | `tests/golden-hdr/cesium-milk-truck/s04-default/1280x720/hdr-scene-color.rlhdr` | [`hdr-regression.md`](hdr-regression.md) §6 |
| Tone-mapped LDR | same directory, `final.png` (plus `lighting-lit.png`, a non-compared review thumbnail) | [`hdr-regression.md`](hdr-regression.md) §6a |
| Capture identity | same directory, `hdr-capture-metadata.json` (schema v2) + `manifest.json` | [`hdr-regression.md`](hdr-regression.md) §7 |
| CPU float64 chain | `scripts/postprocess_reference.py` (independent reference) and `scripts/verify_final_vs_reference.py` (end-to-end `final.png` verifier) | [`postprocess.md`](postprocess.md) §9 |

Layered checks for the Stage 5 migration: GBuffer content (PNG oracle),
GBuffer→lighting integration (`.rlhdr` float oracle), lighting→present
(`final.png` 8-bit oracle), and the full chain against a float64 CPU reference
(`verify_final_vs_reference.py`). No raw GBuffer binary dump exists; the
`.rlhdr` oracle covers GBuffer float content transitively through lighting.

## 2. Re-verification

Run on the M1 commit (all must pass):

```powershell
cmake --build --preset windows-debug --parallel
cmake --build --preset windows-release --parallel
.\out\build\windows-vs2022\bin\Debug\RenderLabDataContractTests.exe
.\out\build\windows-vs2022\bin\Release\RenderLabDataContractTests.exe
powershell -NoProfile -File scripts\golden.ps1 -Mode Verify -Configuration Debug
powershell -NoProfile -File scripts\golden-hdr.ps1 -Mode Verify -Configuration Debug
powershell -NoProfile -File scripts\smoke.ps1
python scripts\verify_final_vs_reference.py tests\golden-hdr\cesium-milk-truck\s04-default\1280x720
```

GPU evidence (local, not CI): one Debug PIX frame showing
`Frame > Render > { SceneUpdate, GBuffer, DeferredLighting, PostProcess }`,
then `UI`/`ImGUI`, then `Present` — readable and separate. PIX workflow:
[`capture-guide.md`](capture-guide.md).

## 3. Manual pass order

All GPU passes are recorded on the app command list in
`RenderingLabApp::RenderScene` (`src/app/RenderingLabApp.cpp`); markers are the
constants in `src/app/FrameMarkers.h`.

```text
Frame (CPU)
  SceneUpdate            Scene::Refresh, RebuildDrawList, UpdateFrameViewConstants
  Render
    GBuffer              GBufferPass::Execute — clears + opaque draws into
                         GBufferA/B/C + GBufferDepth (timestamped)
    DeferredLighting     lighting constants (default or --verify-lights) +
                         point-light clamp, then DeferredLightingPass::Execute —
                         clears HDRSceneColor, fullscreen lighting (timestamped)
    <present-source XOR> exactly one of:
                         PostProcess    (default, PresentSource::Final)
                         LightingDebug  (--lighting-view)
                         GBufferDebug   (--gbuffer-view)
  UI / ImGUI             Donut ImGui overlay on the same back buffer
  Present                standalone GPU range + DXGI present
```

The three timed passes run unconditionally; only the present-source branch is
exclusive. If `HDRSceneColor` or the framebuffer color is missing the frame
falls back to GBufferDebug-only or a clear (`RenderScene` error paths).

## 4. Persistent resources and states

Owned by `RenderingLabApp` (`src/app/RenderingLabApp.h`), created in
`BackBufferResized`, released in `BackBufferResizing`:

| Resource | Format | `initialState` | `keepInitialState` | Notes |
|---|---|---|---|---|
| `GBufferA` | `SRGBA8_UNORM` | RenderTarget | true | base color (sRGB) |
| `GBufferB` | `RGBA16_FLOAT` | RenderTarget | true | world normal + roughness |
| `GBufferC` | `RGBA8_UNORM` | RenderTarget | true | metallic / AO / flags |
| `GBufferDepth` | `R32_TYPELESS` (`D32_FLOAT` DSV + `R32_FLOAT` SRV) | DepthWrite | true | reversed-Z |
| `HDRSceneColor` | `RGBA16_FLOAT` | RenderTarget | true | pre-exposure scene color |
| dump targets | `SRGBA8_UNORM` | RenderTarget | true | `--dump-*` / `--output-hdr` only, created on demand |

Per-pass size-dependent state (framebuffers, pipelines, binding caches, the
PostProcess dump target) is held by the pass classes and dropped in each
pass's `ReleaseSizeDependentResources()`.

Resize release order in `BackBufferResizing` (consumers before producers,
before `DeviceManager_DX12::ResizeSwapChain` waits for idle and runs NVRHI
garbage collection):

```text
postProcess -> lightingDebug -> gbufferDebug -> deferredLighting
           -> gbufferPass -> hdrSceneColor -> gbuffer
```

`BackBufferResized` then recreates `m_gbuffer` first and `m_hdrSceneColor`
second. Minimized (zero-size) frames skip recreation; the targets are retained.

## 5. Per-pass GPU timing

Each timed pass (GBuffer, DeferredLighting, PostProcess) owns a 4-deep
`nvrhi::TimerQueryHandle` ring. At the head of `Execute` the pass polls the
oldest resolvable query into its HUD (`gpuTimeMilliseconds`,
`timestampValid`) and begins a new one; the ImGui diagnostics panel displays
the values live.

`--output-hdr` persists them (S3.3): `DumpHdrCapture` calls
`device->waitForIdle()`, then `ResolvePendingTimerQueries()` on all three
passes **before** the dump's own re-renders begin new queries, so
`hdr-capture-metadata.json` records the captured frame's (frame 1) per-pass
times — `gBufferGpuTimeMilliseconds`, `deferredLightingGpuTimeMilliseconds`,
`postProcessGpuTimeMilliseconds`. The comparator ignores these fields; they
are evidence, not a gate ([`hdr-regression.md`](hdr-regression.md) §7).

## 6. Freeze policy

- No new rasterization features after M1 unless RDG/DXR requires them
  (`IMPLEMENTATION_PLAN.md` §7, S3.3).
- Frozen math, do not touch: `src/shaders/postprocess.hlsli`,
  `src/renderer/PostProcessContract.h`, `src/shaders/postprocess_cb.h`.
- Frozen golden contracts: `image-regression.md` (S1.6 `--output` stays
  GBuffer-only), `hdr-regression.md` (schema v2, locked identity).
- Marker names are stable across Debug and Release; do not rename
  ([`capture-guide.md`](capture-guide.md)).
- The M1 numbers (per-pass ms, PIX evidence) are recorded in
  [`PROGRESS.md`](PROGRESS.md); they are machine-specific evidence, not gates.
