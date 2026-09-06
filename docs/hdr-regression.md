# HDR Regression (S2.4)

Status: **frozen after S2.4**

This file is the Stage 2 HDR capture and comparison contract. Lighting spaces,
BRDF, and `HDRSceneColor` live in [`lighting.md`](lighting.md). GBuffer PNG
regression stays in [`image-regression.md`](image-regression.md) and is not
extended by this step.

S2.4 exists to catch math, color-space, and integration regressions in the
deferred HDR target. It does not introduce a tone mapper, change default present,
or replace S1.6.

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
7. On success, writes the three files in section 2.

HDR tests should use `--headless --lock-camera --output-hdr <dir>`.

`--dump-lighting-views` remains a visualization dump without metadata and is not
this harness. `--verify-lights` and `--scene fallback-boxes` remain a metal /
point-light checklist. They are not a second MAE tree.

## 2. Files written

On a successful capture, `<dir>` contains exactly:

| File | Role | Compared |
|---|---|---|
| `hdr-scene-color.rlhdr` | Scene-referred `HDRSceneColor` dump | Yes |
| `lighting-lit.png` | Reinhard `hdr/(1+hdr)` thumbnail | No |
| `hdr-capture-metadata.json` | Identity, adapter, lighting, finite counts, optional GPU time | Identity only |

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

The C++ rules in the HDR compare sources are authoritative once implemented.
A human `manifest.json` under the golden directory mirrors them.

Pixel failures:

| Situation | Verdict | Exit code |
|---|---|---:|
| Within tight tolerances | `pass` | 0 |
| Adapter/driver match the approved golden and the dump exceeds tight tolerances | `regression` | 1 |
| Adapter/driver differ, tight fails, portability band still holds | `portability` | 2 |
| Adapter/driver differ and the dump exceeds the portability band | `regression` | 1 |
| IO / missing files / invalid `.rlhdr` / invalid JSON / non-finite RGB | `error` | 3 |

`--mode hdr --channel-swap` swaps candidate R and B after decode and must
**fail** (non-zero). `golden-hdr.ps1 Verify` requires that failure.

Default `RenderLabGoldenCompare.exe` mode remains S1.6 GBuffer PNG compare.
`--mode hdr` selects this contract. Passing an S1.6 directory to `--mode hdr`
is an **error**.

## 7. Metadata

`--output-hdr` writes `hdr-capture-metadata.json`. Schema
`renderlab-hdr-capture-metadata/v1`. Do not reuse `capture-metadata.json` or
schema `renderlab-capture-metadata/v1`.

Required fields and types:

| Key | Type | Locked value / rule |
|---|---|---|
| `schema` | string | `renderlab-hdr-capture-metadata/v1` |
| `step` | string | `S2.4` |
| `sceneId` | string | `cesium-milk-truck` |
| `cameraPreset` | string | `s04-default` |
| `width` | uint32 | `1280` |
| `height` | uint32 | `720` |
| `frameIndex` | uint32 | `1` |
| `sampleCount` | uint32 | `1` |
| `verifyLights` | bool | `false` |
| `adapterName` | string | capturing GPU; first approved capture defines the baseline |
| `driverVersion` | string | capturing driver |
| `hdrFileName` | string | `hdr-scene-color.rlhdr` |
| `diagnosticFileName` | string | `lighting-lit.png` |
| `nonFiniteCount` | uint32 | `0` on any file that was actually written |
| `pixelCount` | uint32 | `921600` |
| `timestampValid` | bool | best-effort; `false` on frame 1 is allowed |
| `deferredLightingGpuTimeMilliseconds` | number | present only when `timestampValid` is true; JSON floating number, not an integer token requirement |

The comparator parses the complete document with jsoncpp. Trailing garbage,
wrong types, and integral reals such as `1280.0` for uint32 fields are
**error**. Empty metadata is not a wildcard.

Present identity fields that do not match the locked scene, camera, resolution,
frame, schema, sample count, or `verifyLights: false` are a **regression**, even
if the `.rlhdr` pixels match.

GPU time does not fail the capture when the timer query is still pending.

The approved HDR adapter is the machine that mints the first committed
`.rlhdr`. It is not required to match the S1.6 GBuffer golden adapter.

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

One local PIX frame is recorded in [`PROGRESS.md`](PROGRESS.md) when S2.4
completes. Confirm under `Render`:

- `GBuffer` reads as already documented (clears + opaque draws)
- `DeferredLighting` reads the declared GBuffer SRVs and writes `HDRSceneColor`
- No extra lighting UAVs, no second HDR target, no compute lighting

This is evidence, not a CI gate.

## 11. Explicit exclusions

- Extending `--output` or `scripts\golden.ps1`
- TinyEXR, DDS, PFM, or PNG as the HDR oracle
- Comparing `lighting-lit.png`
- C++ `EvaluateDirectBRDF` / dual-maintained GGX
- A second golden for `--verify-lights` or `fallback-boxes`
- Tone mapping, exposure, or changing default present (S3)
- Requiring a valid DeferredLighting timestamp on frame 1
