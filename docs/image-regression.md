# Image Regression (S1.6)

Status: **frozen for Stage 1 after S1.6**

This file is the first GBuffer image-regression contract. It sits on top of the
S1.5 debug visualization dump. It does not invent a second packing, a second
screenshot path, lighting, or tone mapping.

## 1. Locked capture

| Input | Value |
|---|---|
| Scene | `cesium-milk-truck` (default) |
| Camera | preset `s04-default`, `--lock-camera` |
| Resolution | 1280×720 |
| Frame | 1 |
| Sample count | 1 |

`--output <dir>` is the golden harness:

1. Implies `--lock-camera`.
2. Disables the windowed `--frames` resize/minimize probe so the image stays 1280×720.
3. Reuses `DumpGBufferDebugViews` / `GBufferDebugColor` / Donut `SaveTextureToFile`.
4. Writes the six ADR-002 PNG names plus `capture-metadata.json`.

`--dump-gbuffer-views` remains a visualization dump without metadata. Image tests
should use `--headless --lock-camera --output <dir>`.

The PNG files are display-referred 8-bit dumps of visualized channels. Base color
uses the hardware sRGB OETF on `GBufferDebugColor`. That encoding is not lighting
and is not the S3 tone map.

## 2. Approved goldens

Committed references live in
[`tests/golden/cesium-milk-truck/s04-default/1280x720/`](../tests/golden/cesium-milk-truck/s04-default/1280x720/).

Generated re-captures, reports, and working copies go under `/results/` and must
not be committed. `/results/` and `/captures/` are gitignored.

## 3. Why comparison is not exact byte equality

The dumps are 8-bit visualizations of GPU data:

- Base color goes through an sRGB render target and PNG.
- World normals are float16 remapped with `0.5 * n + 0.5`.
- Linearized depth is `viewZ / (viewZ + 1)` from reversed-Z `D32` data.
- PNG encoding, driver, and Debug vs Release can change bytes without a renderer bug.

Comparison therefore decodes RGB pixels and applies per-view MAE and mismatch-fraction
tolerances. Exact PNG SHA-256 is not a pass/fail gate.

## 4. Per-output rules

Metrics are computed in 8-bit RGB (0–255). A pixel mismatches when any channel
differs by more than `pixelThreshold`. `mae` is the mean absolute error across
all pixels and channels. `mismatchFraction` is mismatched pixels / pixel count.

Images must be 1280×720.

| View | pixelThreshold | maxMae | maxMismatchFraction | Notes |
|---|---:|---:|---:|---|
| base-color | 2 | 1.0 | 0.002 | Linear albedo visualized as sRGB |
| world-normal | 3 | 1.5 | 0.005 | Remapped float16; background black |
| roughness | 2 | 1.0 | 0.002 | Perceptual grayscale |
| metallic | 2 | 1.0 | 0.002 | **Weak oracle** (Milk Truck metallic = 0) |
| ao-flags | 2 | 1.0 | 0.002 | AO / ShadingValid / TwoSided |
| linear-depth | 4 | 2.0 | 0.010 | `viewZ/(viewZ+1)`; background magenta |

Portability band (used only when adapter or driver differs from the approved
metadata):

| View | pixelThreshold | maxMae | maxMismatchFraction |
|---|---:|---:|---:|
| base-color, roughness, metallic, ao-flags | 8 | 8.0 | 0.05 |
| world-normal | 10 | 10.0 | 0.08 |
| linear-depth | 12 | 12.0 | 0.10 |

The C++ rules in `tests/image_compare.cpp` are authoritative. `tests/golden/manifest.json`
mirrors them for humans.

## 5. Adapter / driver metadata and portability

`--output` writes `capture-metadata.json` beside the PNGs. Required identity
fields:

- `schema` (`renderlab-capture-metadata/v1`)
- `sceneId`, `cameraPreset`
- `width`, `height`, `frameIndex`, `sampleCount`
- `adapterName`, `driverVersion`

The comparator parses the complete document with Donut's jsoncpp. It does not
scan for keys or stop at the first digit run. A file that is not valid JSON —
including trailing garbage or a token such as `1280oops` — is an **error**.
Required fields must be present with the correct JSON types (strings or
non-negative integers). Empty metadata is not a wildcard.

Identity is then checked against the locked S1.6 constants. Present identity
fields that do not match the locked scene, camera, resolution, frame, schema,
or sample count are a **regression**, even if the PNGs match.

Pixel failures are classified as:

| Situation | Verdict | Exit code |
|---|---|---:|
| All views within tight tolerances | `pass` | 0 |
| Adapter/driver match the approved golden and a view exceeds tight tolerances | `regression` | 1 |
| Adapter/driver differ, tight tolerances fail, portability band still holds | `portability` | 2 |
| Adapter/driver differ and a view exceeds the portability band | `regression` | 1 |
| IO / missing files / invalid PNG | `error` | 3 |

Portability means "this GPU/driver is not the approved environment", not "the
renderer is correct on every vendor". The first baseline was captured on the
S0.1 host GPU recorded in `capture-metadata.json`.

## 6. Metallic weak oracle

Cesium Milk Truck metallic is 0, so the metallic dump is near-black. Fallback
`GoldMetal` exists but is a poor fit for the locked S0.4 look-at. S1.6 keeps
Milk Truck and treats metallic as a weak check: it must stay near-black and
must fail if swapped with a non-black view. It is not a metallic-material oracle.

## 7. Commands

```powershell
cmake --build --preset windows-debug --parallel
.\out\build\windows-vs2022\bin\Debug\RenderLab.exe --headless --lock-camera --output results\s16-run1
.\out\build\windows-vs2022\bin\Debug\RenderLabGoldenCompare.exe --candidate results\s16-run1 --reference tests\golden\cesium-milk-truck\s04-default\1280x720
.\out\build\windows-vs2022\bin\Debug\RenderLabGoldenCompare.exe --channel-swap --candidate results\s16-run1 --reference tests\golden\cesium-milk-truck\s04-default\1280x720
powershell -NoProfile -File scripts\golden.ps1
```

`scripts\golden.ps1` (default `Verify`) captures twice, compares both runs to
the committed goldens, and proves the roughness-as-base-color swap fails.

CPU tests in `RenderLabDataContractTests` cover the comparison rules, a
synthetic R/B swap, self-compare of committed goldens, and the channel-swap
proof on those files. They do not need a GPU.

GitHub-hosted `windows-2022` has no NVIDIA GPU. CI stays configure + build +
CPU tests. GPU image comparison is a documented local/self-hosted test, same
as `scripts\smoke.ps1`.
