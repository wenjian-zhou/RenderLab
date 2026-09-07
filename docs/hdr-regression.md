# HDR Regression (S2.4 + S3.2 LDR golden)

Status: **frozen after S2.4; extended by S3.2** (final.png + schema v2)

This file is the Stage 2/3 HDR and tone-mapped capture and comparison contract.
Lighting spaces, BRDF, and `HDRSceneColor` live in [`lighting.md`](lighting.md).
The tone curve and output transfer live in [`postprocess.md`](postprocess.md).
GBuffer PNG regression stays in [`image-regression.md`](image-regression.md) and
is not extended by this step.

S2.4 exists to catch math, color-space, and integration regressions in the
deferred HDR target. S3.2 extends the same capture bundle with a
display-referred LDR oracle (`final.png`) without touching the pre-exposure
`.rlhdr` oracle.

## 1. Locked capture

Same identity as S1.6. Default lights. Not `--verify-lights`.

| Input | Value |
|---|---|
| Scene | `cesium-milk-truck` (default) |
| Camera | preset `s04-default`, `--lock-camera` |
| Resolution | 1280×720 |
| Frame | 1 |
| Sample count | 1 |
| Lights | `MakeDefaultLightingConstants()` (directional only, ambient 0, `pointLightCount` 0) |
| `verifyLights` | `false` |

`--output-hdr <dir>` is the golden harness:

1. Implies `--lock-camera`.
2. Disables the windowed `--frames` resize/minimize probe so the image stays 1280×720.
3. Is mutually exclusive with `--output`.
4. Is orthogonal to `--gbuffer-view`, `--lighting-view`, `--dump-gbuffer-views`, and `--dump-lighting-views`.
5. Readbacks `HDRSceneColor` after DeferredLighting on frame 1. It does not blit through `SaveTextureToFile`.
6. Scans RGB for non-finite values. On any NaN or Inf: log, exit non-zero, **write no files** under `<dir>`.
7. On success, writes the four files in section 2. `final.png` is rendered by
   `PostProcessPass` into a `SRGBA8_UNORM` dump target — the same hardware OETF
   path as the presented back buffer — at the capture's `--exposure-ev`.

HDR tests should use `--headless --lock-camera --output-hdr <dir>`.

`--dump-lighting-views` remains a visualization dump without metadata and is not
this harness. `--verify-lights` and `--scene fallback-boxes` remain a metal /
point-light checklist. They are not a second MAE tree.

## 2. Files written

On a successful capture, `<dir>` contains exactly:

| File | Role | Compared |
|---|---|---|
| `hdr-scene-color.rlhdr` | Scene-referred `HDRSceneColor` dump (pre-exposure) | Yes, float rule (section 6) |
| `lighting-lit.png` | Reinhard `hdr/(1+hdr)` thumbnail | No |
| `final.png` | S3.2 tone-mapped final through the hardware OETF, at the capture's exposureEV | Yes, 8-bit rule (section 6) |
| `hdr-capture-metadata.json` | Identity, adapter, lighting, exposure, finite counts, optional GPU time | Identity only |

Committed references live in
[`tests/golden-hdr/cesium-milk-truck/s04-default/1280x720/`](../tests/golden-hdr/cesium-milk-truck/s04-default/1280x720/)
after the first approved capture.

Generated re-captures, reports, and working copies go under `/results/` and must
not be committed. `/results/` and `/captures/` are gitignored.

`lighting-lit.png` is a review thumbnail. The comparator must not load it. The
encoding is visualization only and is not the S3 tone map.

Mark `*.rlhdr` as binary in [`.gitattributes`](../.gitattributes).

## 3. `.rlhdr` layout

Little-endian. Header is 32 bytes, then tightly packed pixels. No row padding,
no checksum, no compression.

| Offset | Type | Field | Value |
|---:|---|---|---|
| 0 | `char[8]` | `magic` | `RLHDR1\0\0` (`52 4C 48 44 52 31 00 00`) |
| 8 | `uint32` | `version` | `1` |
| 12 | `uint32` | `width` | `1280` for the locked capture |
| 16 | `uint32` | `height` | `720` for the locked capture |
| 20 | `uint32` | `format` | `1` = `RGBA16_FLOAT` |
| 24 | `uint32` | `reserved0` | `0` |
| 28 | `uint32` | `reserved1` | `0` |
| 32 | pixels | | `width * height` texels |

Each texel is 8 bytes: IEEE-754 binary16 `R, G, B, A` in that order, matching
`nvrhi::Format::RGBA16_FLOAT`. Alpha is unused in lighting (writers store `1`)
and is ignored by compare and the finite scan.

Locked-capture file size is `32 + 1280 * 720 * 8 = 7,372,832` bytes.

A file is invalid (comparator **error**, exit 3) when magic, version, format, or
reserved fields do not match, when width/height are not the locked 1280×720 for
this golden, or when the file size does not equal `32 + width * height * 8`.

## 4. Non-finite policy

A texel is non-finite when any of **R, G, B**, promoted to `float32`, is NaN or
Inf. Alpha is not tested.

- `RenderLab.exe --output-hdr` must not write `hdr-scene-color.rlhdr`,
  `lighting-lit.png`, or `hdr-capture-metadata.json` if `nonFiniteCount > 0`.
  Exit non-zero.
- `RenderLabGoldenCompare.exe --mode hdr` treats non-finite RGB in the candidate
  **or** the reference as **error** (exit 3), not as MAE.

This is the S2.4 proof that grazing angles and minimum roughness stay finite on
the real deferred output. There is no C++ port of `EvaluateDirectBRDF`.

## 5. Why comparison is not exact byte equality

`HDRSceneColor` is GPU `RGBA16_FLOAT`. Debug vs Release, driver, and denorm
flush can change bits without a lighting bug. Exact file SHA-256 is not a
pass/fail gate.

The comparator promotes RGB halves to `float32` and applies the mixed absolute /
relative rule below.

## 6. Compare rule

RGB only. Images must be 1280×720. `relativeFloor` is part of the formula and is
frozen here at `1e-4`.

For each pixel and each of R, G, B:

```text
absErr = |candidate - reference|
relDen = max(|reference|, relativeFloor)
pixelChannelMismatches = (absErr > absTol) AND (absErr > relTol * relDen)
```

A pixel mismatches when any of its RGB channels mismatches.

- `mae` is the mean of `absErr` across all pixels and the three RGB channels.
- `mismatchFraction` is mismatched pixels / pixel count.

Tight and portability **numeric** `absTol`, `relTol`, `maxMae`, and
`maxMismatchFraction` were taken from the first approved capture on this
machine (two Debug captures, mae 0, mismatchFraction 0) and written into this
table, `tests/hdr_compare.h`, and
`tests/golden-hdr/cesium-milk-truck/s04-default/1280x720/manifest.json`.
`golden-hdr.ps1 Verify` is an exit gate against those goldens.

| Band | absTol | relTol | maxMae | maxMismatchFraction |
|---|---:|---:|---:|---:|
| Tight | 1e-4 | 1e-3 | 1e-4 | 0.002 |
| Portability | 5e-4 | 5e-3 | 1e-3 | 0.02 |

Portability is used only when `adapterName` or `driverVersion` differ from the
approved metadata.

### 6a. final.png 8-bit rule (S3.2)

`final.png` is compared with the S1.6 GBuffer-golden mechanism
(`tests/image_compare.cpp` `LoadPngRgb` / `CompareRgb` / `StatsPass`) and the
S1.6 default tolerances; the two same-adapter approval captures were mae 0 /
mismatch 0, so the defaults are also the floor. A pixel mismatches when any RGB
channel differs by more than `pixelThreshold` (0-255 units). `maxAbs` is
reported, not gated.

| Band | pixelThreshold | maxMae | maxMismatchFraction |
|---|---:|---:|---:|
| Tight | 2 | 1.0 | 0.002 |
| Portability | 8 | 8.0 | 0.05 |

The overall verdict combines both oracles: **pass** requires the `.rlhdr` float
rule **and** the `final.png` tight rule; the portability band applies only when
the adapter/driver differs and **both** oracles stay within it. Identity fields
(section 7) still fail as **regression** even when all pixels match.

The C++ rules in the HDR compare sources are authoritative once implemented.
A human `manifest.json` under the golden directory mirrors them.

Pixel failures:

| Situation | Verdict | Exit code |
|---|---|---:|
| Both `.rlhdr` and `final.png` within tight tolerances | `pass` | 0 |
| Adapter/driver match the approved golden and either oracle exceeds tight tolerances | `regression` | 1 |
| Adapter/driver differ, tight fails, both oracles still within the portability band | `portability` | 2 |
| Adapter/driver differ and either oracle exceeds the portability band | `regression` | 1 |
| IO / missing files / invalid `.rlhdr` / missing `final.png` / invalid JSON / non-finite RGB | `error` | 3 |

`--mode hdr --channel-swap` swaps candidate R and B after decode and must
**fail** (non-zero). `golden-hdr.ps1 Verify` requires that failure.

Default `RenderLabGoldenCompare.exe` mode remains S1.6 GBuffer PNG compare.
`--mode hdr` selects this contract. Passing an S1.6 directory to `--mode hdr`
is an **error**.

## 7. Metadata

`--output-hdr` writes `hdr-capture-metadata.json`. Schema
`renderlab-hdr-capture-metadata/v2` (S3.2; v1 is rejected). Do not reuse
`capture-metadata.json` or schema `renderlab-capture-metadata/v1`.

Required fields and types:

| Key | Type | Locked value / rule |
|---|---|---|
| `schema` | string | `renderlab-hdr-capture-metadata/v2` |
| `step` | string | `S3.2` |
| `sceneId` | string | `cesium-milk-truck` |
| `cameraPreset` | string | `s04-default` |
| `width` | uint32 | `1280` |
| `height` | uint32 | `720` |
| `frameIndex` | uint32 | `1` |
| `sampleCount` | uint32 | `1` |
| `exposureEV` | number (finite) | `0` for the approved golden; a JSON number, integer or real token both accepted |
| `verifyLights` | bool | `false` |
| `adapterName` | string | capturing GPU; first approved capture defines the baseline |
| `driverVersion` | string | capturing driver |
| `hdrFileName` | string | `hdr-scene-color.rlhdr` |
| `diagnosticFileName` | string | `lighting-lit.png` |
| `finalFileName` | string | `final.png` |
| `nonFiniteCount` | uint32 | `0` on any file that was actually written |
| `pixelCount` | uint32 | `921600` |
| `timestampValid` | bool | best-effort; `false` on frame 1 is allowed |
| `deferredLightingGpuTimeMilliseconds` | number | present only when `timestampValid` is true; JSON floating number, not an integer token requirement |
| `postProcessGpuTimeMilliseconds` | number | optional, same rule as the deferred field; ignored by compare |

The comparator parses the complete document with jsoncpp. Trailing garbage,
wrong types, and integral reals such as `1280.0` for uint32 fields are
**error**. Empty metadata is not a wildcard. A non-finite `exposureEV` token
(e.g. an overflow to Inf) is an **error**.

Present identity fields that do not match the locked scene, camera, resolution,
frame, schema, sample count, `verifyLights: false`, `exposureEV: 0`, or
`finalFileName: final.png` are a **regression**, even if all pixels match —
this is how a capture taken at a non-zero `--exposure-ev` fails the golden
cleanly.

GPU time does not fail the capture when the timer query is still pending.

The approved HDR adapter is the machine that mints the committed `.rlhdr`. The
S2.4 baseline (RTX 5060 Laptop GPU) and the S3.2 re-baseline (RTX 4070 SUPER)
produced a bit-identical `.rlhdr`, so the deferred path is deterministic across
those two adapters. It is not required to match the S1.6 GBuffer golden adapter.

## 8. Weak lighting oracle

Cesium Milk Truck metallic is ~0 and the locked lights are directional-only.
This golden is an integration / finite / color-space check. It is not a metal
or punctual-light oracle. Do not change the S0.4 camera to chase `GoldMetal`.

## 9. Commands

Intended local workflow after implementation:

```powershell
cmake --build --preset windows-debug --parallel
.\out\build\windows-vs2022\bin\Debug\RenderLab.exe --headless --lock-camera --output-hdr results\s24-run1
.\out\build\windows-vs2022\bin\Debug\RenderLabGoldenCompare.exe --mode hdr --candidate results\s24-run1 --reference tests\golden-hdr\cesium-milk-truck\s04-default\1280x720
.\out\build\windows-vs2022\bin\Debug\RenderLabGoldenCompare.exe --mode hdr --channel-swap --candidate results\s24-run1 --reference tests\golden-hdr\cesium-milk-truck\s04-default\1280x720
powershell -NoProfile -File scripts\golden-hdr.ps1
```

`scripts\golden-hdr.ps1` (default `Verify`) captures twice, compares both runs
to the committed HDR goldens, and proves the R/B swap fails. It does not invoke
`scripts\golden.ps1`.

CPU tests in `RenderLabDataContractTests` cover `.rlhdr` header roundtrip,
finite/non-finite classification, the mixed compare rule, metadata identity,
and a synthetic R/B swap. They do not need a GPU and do not port the HLSL BRDF.

GitHub-hosted `windows-2022` has no NVIDIA GPU. CI stays configure + build +
CPU tests. GPU HDR comparison is a documented local/self-hosted test.

## 10. PIX evidence (not automated)

One local PIX frame is recorded in [`PROGRESS.md`](PROGRESS.md) when S2.4 and
S3.2 complete. Confirm under `Render`:

- `GBuffer` reads as already documented (clears + opaque draws)
- `DeferredLighting` reads the declared GBuffer SRVs and writes `HDRSceneColor`
- S3.2: `PostProcess` follows `DeferredLighting`, reads `HDRSceneColor`, and
  writes the back buffer (or the `PostProcessColor` dump target during a
  capture); `UI` / `ImGUI` then draw on top of the tone-mapped output
- No extra lighting UAVs, no second HDR target, no compute lighting

This is evidence, not a CI gate.

## 11. Explicit exclusions

- Extending `--output` or `scripts\golden.ps1`
- TinyEXR, DDS, PFM, or PNG as the **HDR** oracle (`final.png` is the separate
  display-referred LDR oracle, not a replacement for `.rlhdr`)
- Comparing `lighting-lit.png`
- C++ `EvaluateDirectBRDF` / dual-maintained GGX
- A second golden for `--verify-lights` or `fallback-boxes`
- Automatic exposure, bloom, color grading, FXAA/TAA, HDR display output
  (postprocess.md section 8)
- Requiring a valid DeferredLighting timestamp on frame 1
