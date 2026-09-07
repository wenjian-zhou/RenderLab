# Post Processing: Exposure and Output Transfer

Status: **frozen for Stage 3**

Step: S3.1

This file freezes where HDR scene color becomes display-referred output: the exposure
parameterization, the tone curve, the single output transfer, and the UI composition
space. S3.2 implements `PostProcessPass` against this contract; S3.3 freezes the
resulting manual pipeline as the RDG reference.

Implementations:

- CPU mirror and frozen constants: [`../src/renderer/PostProcessContract.h`](../src/renderer/PostProcessContract.h)
- HLSL mirror: [`../src/shaders/postprocess.hlsli`](../src/shaders/postprocess.hlsli)
- Constant buffer: [`../src/shaders/postprocess_cb.h`](../src/shaders/postprocess_cb.h)
- Independent float64 reference and test-vector generator:
  [`../scripts/postprocess_reference.py`](../scripts/postprocess_reference.py)
- Tests: [`../tests/test_postprocess_contract.cpp`](../tests/test_postprocess_contract.cpp)

## 1. Scope

- The normal output path from Stage 3 on: `Scene -> GBuffer -> Deferred -> Exposure +
  Tone Map -> Present`.
- The tone curve is UE 5.8.1's default **Filmic** method, adopted wholesale by decision
  (2026-09-07): expand gamut (1.0), blue correction (0.6), and `FilmToneMap` with UE's
  default parameters. The non-default "Standard ACES" method is not used; the old
  UE4 `ACESFitted` fit no longer exists in UE 5.8.
- Exposure is a single manual EV knob. No automatic exposure.

## 2. The Output Chain

```text
HDRSceneColor (linear Rec.709 / sRGB primaries / D65, scene-referred,
               simplified scene units, exposure-independent)
  |  x exp2(exposureEV)                       manual exposure (default EV 0)
  v
working sRGB -> AP1                           kWorkingToAP1 (Bradford D65->D60 CAT)
  |  expand gamut, amount from chroma/luma    kExpandWide
  |  blue correction, lerp amount 0.6         kBlueCorrectAP1
  |  FilmToneMap                              AP1 -> AP0 -> curve -> AP1
  |  inverse blue correction, lerp amount 0.6 kBlueCorrectInvAP1
  v
AP1 -> working sRGB, max(0, .)               kAP1ToWorking
  |
  v
display-referred linear Rec.709, NOT clamped to [0, 1]
  |
  v  hardware sRGB OETF on store (SRGBA8_UNORM)   <-- the ONLY output transfer
back buffer  ->  ImGui composition (same buffer)  ->  Present
```

Composition and apply order match UE's `CombineLUTsCommon` / `ComputeFilmColorNoGamma`
(`Engine/Shaders/Private/PostProcessCombineLUTs.usf:160-234, 344-481`) with default
settings. Matrices are stored transposed for RenderLab's row-vector convention
([`renderer-conventions.md`](renderer-conventions.md) section 2): multiply as
`mul(color, M)` in HLSL and `color * M` in C++; each literal is the UE matrix
transposed, so the result equals UE's column-vector `mul(M_UE, color)`.

## 3. Exposure

| Property | Value |
|---|---|
| Parameter | `TonemapConstants.exposureEV` (float, CB offset 0) |
| Mapping | linear scale = `exp2(exposureEV)`; +1 EV doubles brightness |
| Default | `0` (scale 1) |
| Clamp | none at the contract level (UE has no runtime clamp either; its editor UI suggests ±15) |
| Physical camera | none. No f-number / shutter / ISO. |
| Position | pre-multiply, before the tone curve (UE applies exposure before the LUT lookup, `PostProcessTonemap.usf:523`) |

This is UE's manual-exposure semantics exactly: `AutoExposureBias` is logarithmic in EV
stops (`Engine/Source/Runtime/Engine/Classes/Engine/Scene.h:1928-1933`, "0: no
adjustment, -1:2x darker, 1:2x brighter"), the multiplier is `2^bias`
(`PostProcessEyeAdaptation.cpp:446-460`), and with the physical camera disabled the
manual path reduces to exactly that multiplier ("AEM_Manual ExposureCompensation is
already calibrated to 1.0", `PostProcessEyeAdaptation.cpp:626`). The photometric
EV100 / LuminanceMax machinery is not adopted because RenderLab lights are simplified
scene units, not photometric ([`lighting.md`](lighting.md) section on units).

CLI name reserved for S3.2: `--exposure-ev`.

## 4. Tone Curve (UE 5.8.1 Filmic)

All constants are frozen; none are constant-buffer state. The single runtime knob is
exposure.

### 4.1 Frozen constants

| Constant | Value | UE source |
|---|---|---|
| FilmSlope / FilmToe / FilmShoulder / FilmBlackClip / FilmWhiteClip | 0.88 / 0.55 / 0.26 / 0.0 / 0.04 | `Engine/Source/Runtime/Engine/Private/Scene.cpp:445-449` |
| BlueCorrection (lerp amount) | 0.6 | `Scene.cpp:447` |
| ExpandGamut (strength) | 1.0 | `Scene.cpp:438` |
| RRT glow gain / mid | 0.05 / 0.08 | `Engine/Shaders/Private/TonemapCommon.ush:151-152` |
| RRT red scale / pivot / hue / width | 0.82 / 0.03 / 0 / 135 | `TonemapCommon.ush:163-166` |
| Pre / post desaturation | 0.96 / 0.93 | `TonemapCommon.ush:180, 221` |
| Log-curve in/out match | 0.18 / 0.18 | `TonemapCommon.ush:185-186` |
| YC radius weight | 1.75 | `Engine/Shaders/Private/ACES/ACESCommon.ush:268` |

### 4.2 FilmToneMap structure

`FilmToneMapAP1` (UE `FilmToneMap`, `TonemapCommon.ush:110-225`) takes and returns AP1:

1. AP1 -> AP0 (`kAP1ToAP0`).
2. RRT glow: `s = sigmoid_shaper((saturation - 0.4) / 0.2)`,
   `color *= 1 + glow_fwd(yc, 0.05 * s, 0.08)`.
3. RRT red modifier around hue 0, width 135, scale 0.82, pivot 0.03.
4. AP0 -> AP1, `max(0, .)`, pre-desaturate toward AP1 luminance with 0.96.
5. Log10 toe/straight/shoulder film curve (solves ToeMatch so input 0.18 maps to
   output 0.18), smoothstep-blended per channel.
6. Post-desaturate with 0.93, `max(0, .)`, return AP1.

Working-space conversion: `kWorkingToAP1 = kSRGBToXYZ * kD65ToD60CAT * kXYZToAP1`,
which is UE's `WorkingColorSpace.ToAP1` for the default sRGB working space
(`Engine/Source/Runtime/Engine/Private/SceneManagement.cpp:154-155`,
`Engine/Source/Runtime/Core/Private/ColorManagement/ColorSpace.cpp:371-382`; default
chromatic adaptation method is Bradford, `ColorManagementDefines.h:110`). The return
trip `kAP1ToWorking` composes the corresponding literal inverses, like
`TonemapCommon.ush:116`. Round-trip identity error is ~2.5e-10 (float64 reference).

Expand gamut (`PostProcessCombineLUTs.usf:377-401`):
`ExpandAmount = (1 - exp2(-4 * chromaDistSqr)) * (1 - exp2(-4 * 1.0 * luma * luma))`
with `chroma = colorAP1 / luma`, `chromaDistSqr = dot(chroma - 1, chroma - 1)`, lerping
toward `kExpandWide = (XYZ->AP1 * Wide->XYZ) * (AP1->sRGB)`.

Blue correction (`PostProcessCombineLUTs.usf:165-188`): lerp toward
`kBlueCorrectAP1 = AP0ToAP1 * BlueCorrect * AP1ToAP0` with amount 0.6 before the
curve, and toward the conjugated inverse after it, to keep the white point.

### 4.3 Properties (verified by tests)

- Mid-gray: `FilmToneMap(0.18 AP1 neutral) = 0.18` exactly; the full chain maps
  sRGB 0.18 gray to ~0.18 (deviation ~1e-6 from the CAT / blue-correction round trips).
- Monotonic: the gray ramp 0.001..100 is non-decreasing; the shoulder asymptote is
  `1 + FilmWhiteClip = 1.04`.
- Not clamped: display-referred output may exceed 1 (e.g. saturated HDR orange reaches
  ~1.21 after blue uncorrection). The `SRGBA8_UNORM` render target saturates on store;
  no explicit clamp exists in the chain.
- Black maps to exactly `(0, 0, 0)`.

## 5. Output Transfer and UI Composition

There is exactly one linear->sRGB transfer in the pipeline: the **hardware sRGB OETF
on store to the `SRGBA8_UNORM` back buffer** (`renderer-conventions.md` section 6).
The tone-map pass writes display-referred *linear* values and does no encoding of its
own. This differs from UE only in location, not in curve: UE encodes with the same
piecewise IEC sRGB function inside its LUT bake (`PostProcessCombineLUTs.usf:416`,
`GammaCorrectionCommon.ush:16-54`).

Audit of every writer to the back buffer (each encodes exactly once, via the hardware
OETF):

| Writer | Path | Encodes |
|---|---|---|
| `GBufferDebugPass` (S1.5) | `--gbuffer-view` / debug dumps | writes visualization values; hardware OETF once |
| `LightingDebugPass` (S2.2/S2.3) | default present, `--lighting-view`, dumps | visualization values (lit view = Reinhard `hdr/(1+hdr)`, not this contract); hardware OETF once |
| `PostProcessPass` (S3.2, planned) | normal path | display-referred linear; hardware OETF once |
| Donut ImGui (after app `Render`, `DeviceManager.cpp:602-608`) | UI overlay | pass-through UI colors; hardware OETF once |

UI composition space: ImGui draws **after** the tone-map output onto the same sRGB
back buffer, with SrcAlpha/InvSrcAlpha blending and no color conversion
(`imgui_nvrhi.cpp:311-401`). UI is display-referred, shares the single hardware OETF,
and never touches HDR scene color. There is no separate UI render target.

The device does not clear the back buffer; a RenderLab fullscreen pass covers every
pixel each frame.

## 6. UE Stages Not Adopted (identity under UE defaults)

| UE stage | Default behavior | Status |
|---|---|---|
| White balance (`CombineLUTsCommon:367-373`) | 6500K / tint 0: branch not taken | skipped |
| Grading `ColorCorrectAll` / `ColorCorrection` / `ColorScale` / `OverlayColor` | all-neutral defaults are an exact identity | skipped (color grading is postponed, section 8) |
| `pow(color, InverseGamma.y)` (`ApplyToneCurve:305`) | 2.2 / displayGamma 2.2 = 1 | skipped |
| AP1 -> output-gamut matrix (`ApplyToneCurve:309-311`) | short-circuited for sRGB working space + sRGB D65 output | skipped |
| `LinearToSrgb` in the LUT bake | Filmic -> sRGB device encode | replaced by the hardware OETF (section 5) |
| 32^3 grading LUT + Log2 shaper | container for the above | not used; the chain is evaluated per pixel |

## 7. RenderLab Adaptations (deviations from literal UE, all verified against the reference)

1. **Matrix convention**: all literals transposed to row-major, row-vector algebra
   (section 2). Numerically identical to UE's column-vector chain.
2. **Expand-gamut guard**: expansion is skipped when AP1 luminance <= 0. UE evaluates
   the chain only on its strictly positive LUT grid; per-pixel black would compute
   `0 / 0 = NaN` in `chroma = colorAP1 / luma`.
3. **Guarded toe/shoulder blend**: the blend selects toe at `t <= 0` and shoulder at
   `t >= 1` instead of a bare `lerp`. UE's `lerp` would produce `inf * 0 = NaN` for
   exact black (`log10(0) = -inf` in the shoulder branch); UE avoids this only because
   its LUT grid never evaluates black. Identical to UE everywhere else.
4. **fp32 evaluation**: UE evaluates the log-curve section in `half` and quantizes
   through the 32^3 LUT; RenderLab evaluates the whole chain in fp32 per pixel. The
   fp32-vs-float64 delta over the reference battery is < 6e-8.

## 8. Postponed (not in Stage 3)

- Automatic exposure (histogram / metering), photometric units.
- Bloom, color grading, FXAA, TAA, local exposure.
- HDR display output (PQ / scRGB / output gamuts beyond sRGB D65).

## 9. Regression Oracles

- The HDR oracle (`.rlhdr`, S2.4) stays **pre-exposure**: `DumpHdrCapture` copies raw
  `HDRSceneColor`, and the post-process pass only reads that texture. Exposure changes
  must not break `golden-hdr.ps1`.
- S3.2 adds a display-referred LDR golden (tone-map output through the same hardware
  OETF path) with exposure pinned in capture metadata. Until then, `--output` /
  `golden.ps1` remain the GBuffer-only oracle and are unaffected by this contract.
- CPU contract tests compare against vectors generated by
  `scripts/postprocess_reference.py` (float64, independent reimplementation of the UE
  chain), not against the contract code itself.
