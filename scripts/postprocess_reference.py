#!/usr/bin/env python3
"""Independent UE 5.8.1 default Filmic tone-map reference (RenderLab S3.1).

Reimplements the frozen chain of docs/postprocess.md in float64, column-vector
convention (M @ v), directly from the UE 5.8.1 sources:

  - FilmToneMap:              Engine/Shaders/Private/TonemapCommon.ush:110-225
  - Base matrices / helpers:  Engine/Shaders/Private/ACES/ACESCommon.ush
  - BlueCorrect / ExpandGamut / composition:
                              Engine/Shaders/Private/PostProcessCombineLUTs.usf:160-234, 344-481
  - WorkingColorSpace.ToAP1 (sRGB -> AP1 incl. Bradford CAT):
                              Engine/Source/Runtime/Engine/Private/SceneManagement.cpp:154-155
                              Engine/Source/Runtime/Core/Private/ColorManagement/ColorSpace.cpp:371-382
                              (DEFAULT_CHROMATIC_ADAPTATION_METHOD = Bradford,
                               ColorManagementDefines.h:110)
  - Film defaults (Slope 0.88 / Toe 0.55 / Shoulder 0.26 / BlackClip 0 /
   WhiteClip 0.04, BlueCorrection 0.6, ExpandGamut 1.0):
                              Engine/Source/Runtime/Engine/Private/Scene.cpp:436-449

UE stages that are exact identities under default settings are NOT part of the
frozen chain: ColorCorrectAll / ColorCorrection / ColorScale / OverlayColor
(default grading), pow(color, InverseGamma.y) with 2.2/2.2, and the AP1 ->
output-gamut matrix short-circuit for sRGB working + sRGB D65 output
(PostProcessCombineLUTs.usf:309-311).

Documented RenderLab adaptations (see docs/postprocess.md):
  1. Matrices are transposed for the row-vector, row-major convention.
  2. Expand gamut is skipped when AP1 luminance <= 0 (UE only evaluates the
     LUT grid, which is strictly positive; per-pixel black would be 0/0 = NaN).
  3. The toe/shoulder blend uses guarded endpoints (t <= 0 -> ToeColor,
     t >= 1 -> ShoulderColor) so exact black (log10(0) = -inf) does not
     produce inf * 0 = NaN inside lerp. Identical to UE on UE's domain.
  4. Evaluation is fp32 on GPU/CPU (UE uses half in the log-curve section and
     quantizes through the 32^3 grading LUT).

Usage: python scripts/postprocess_reference.py
"""

import numpy as np

# ---------------------------------------------------------------------------
# UE 5.8.1 literals (column-vector convention, rows as written in the sources)
# ---------------------------------------------------------------------------

AP0_2_XYZ = np.array([
    [0.9525523959, 0.0000000000, 0.0000936786],
    [0.3439664498, 0.7281660966, -0.0721325464],
    [0.0000000000, 0.0000000000, 1.0088251844]])

XYZ_2_AP0 = np.array([
    [1.0498110175, 0.0000000000, -0.0000974845],
    [-0.4959030231, 1.3733130458, 0.0982400361],
    [0.0000000000, 0.0000000000, 0.9912520182]])

AP1_2_XYZ = np.array([
    [0.6624541811, 0.1340042065, 0.1561876870],
    [0.2722287168, 0.6740817658, 0.0536895174],
    [-0.0055746495, 0.0040607335, 1.0103391003]])

XYZ_2_AP1 = np.array([
    [1.6410233797, -0.3248032942, -0.2364246952],
    [-0.6636628587, 1.6153315917, 0.0167563477],
    [0.0117218943, -0.0082844420, 0.9883948585]])

AP0_2_AP1 = np.array([
    [1.4514393161, -0.2365107469, -0.2149285693],
    [-0.0765537734, 1.1762296998, -0.0996759264],
    [0.0083161484, -0.0060324498, 0.9977163014]])

AP1_2_AP0 = np.array([
    [0.6954522414, 0.1406786965, 0.1638690622],
    [0.0447945634, 0.8596711185, 0.0955343182],
    [-0.0055258826, 0.0040252103, 1.0015006723]])

AP1_RGB2Y = np.array([0.2722287168, 0.6740817658, 0.0536895174])

XYZ_2_sRGB = np.array([
    [3.2409699419, -1.5373831776, -0.4986107603],
    [-0.9692436363, 1.8759675015, 0.0415550574],
    [0.0556300797, -0.2039769589, 1.0569715142]])

SRGB_2_XYZ = np.array([
    [0.4123907993, 0.3575843394, 0.1804807884],
    [0.2126390059, 0.7151686788, 0.0721923154],
    [0.0193308187, 0.1191947798, 0.9505321522]])

D65_2_D60_CAT = np.array([
    [1.0130349146, 0.0061052578, -0.0149709436],
    [0.0076982301, 0.9981633521, -0.0050320385],
    [-0.0028413174, 0.0046851567, 0.9245061375]])

D60_2_D65_CAT = np.array([
    [0.9872240087, -0.0061132286, 0.0159532883],
    [-0.0075983718, 1.0018614847, 0.0053300358],
    [0.0030725771, -0.0050959615, 1.0816806031]])

BLUE_CORRECT = np.array([
    [0.9404372683, -0.0183068787, 0.0778696104],
    [0.0083786969, 0.8286599939, 0.1629613092],
    [0.0005471261, -0.0008833746, 1.0003362486]])

BLUE_CORRECT_INV = np.array([
    [1.06318, 0.0233956, -0.0865726],
    [-0.0106337, 1.20632, -0.19569],
    [-0.000590887, 0.00105248, 0.999538]])

WIDE_2_XYZ = np.array([
    [0.5441691, 0.2395926, 0.1666943],
    [0.2394656, 0.7021530, 0.0583814],
    [-0.0023439, 0.0361834, 1.0552183]])

# Frozen film / chain constants (UE defaults).
FILM_SLOPE = 0.88
FILM_TOE = 0.55
FILM_SHOULDER = 0.26
FILM_BLACK_CLIP = 0.0
FILM_WHITE_CLIP = 0.04
BLUE_CORRECTION = 0.6
EXPAND_GAMUT = 1.0

RRT_GLOW_GAIN = 0.05
RRT_GLOW_MID = 0.08
RRT_RED_SCALE = 0.82
RRT_RED_PIVOT = 0.03
RRT_RED_HUE = 0.0
RRT_RED_WIDTH = 135.0
PRE_DESAT = 0.96
POST_DESAT = 0.93
YC_RADIUS_WEIGHT = 1.75

# Composite matrices (column convention), composed exactly like UE composes them.
TO_AP1 = XYZ_2_AP1 @ D65_2_D60_CAT @ SRGB_2_XYZ           # SceneManagement.cpp:154
FROM_AP1 = XYZ_2_sRGB @ D60_2_D65_CAT @ AP1_2_XYZ         # inverse composition
BLUE_CORRECT_AP1 = AP0_2_AP1 @ BLUE_CORRECT @ AP1_2_AP0   # CombineLUTs.usf:177
BLUE_CORRECT_INV_AP1 = AP0_2_AP1 @ BLUE_CORRECT_INV @ AP1_2_AP0
AP1_2_sRGB = XYZ_2_sRGB @ D60_2_D65_CAT @ AP1_2_XYZ       # CombineLUTs.usf:348
WIDE_2_AP1 = XYZ_2_AP1 @ WIDE_2_XYZ                       # CombineLUTs.usf:397
EXPAND_MAT = WIDE_2_AP1 @ AP1_2_sRGB                      # CombineLUTs.usf:398

# ---------------------------------------------------------------------------
# UE helper functions (ACESCommon.ush / TonemapCommon.ush, scalar math only)
# ---------------------------------------------------------------------------

def rgb_2_saturation(rgb):
    minrgb = min(rgb[0], rgb[1], rgb[2])
    maxrgb = max(rgb[0], rgb[1], rgb[2])
    return (max(maxrgb, 1e-10) - max(minrgb, 1e-10)) / max(maxrgb, 1e-2)

def rgb_2_hue(rgb):
    if rgb[0] == rgb[1] and rgb[1] == rgb[2]:
        hue = 0.0
    else:
        hue = (180.0 / np.pi) * np.arctan2(
            np.sqrt(3.0) * (rgb[1] - rgb[2]), 2.0 * rgb[0] - rgb[1] - rgb[2])
    if hue < 0.0:
        hue = hue + 360.0
    return min(max(hue, 0.0), 360.0)

def center_hue(hue, center_h):
    hue_centered = hue - center_h
    if hue_centered < -180.0:
        hue_centered += 360.0
    elif hue_centered > 180.0:
        hue_centered -= 360.0
    return hue_centered

def rgb_2_yc(rgb, yc_radius_weight=YC_RADIUS_WEIGHT):
    r, g, b = rgb[0], rgb[1], rgb[2]
    chroma = np.sqrt(b * (b - g) + g * (g - r) + r * (r - b))
    return (b + g + r + yc_radius_weight * chroma) / 3.0

def sigmoid_shaper(x):
    t = max(1.0 - abs(0.5 * x), 0.0)
    y = 1.0 + np.sign(x) * (1.0 - t * t)
    return 0.5 * y

def glow_fwd(yc_in, glow_gain_in, glow_mid):
    if yc_in <= 2.0 / 3.0 * glow_mid:
        return glow_gain_in
    if yc_in >= 2.0 * glow_mid:
        return 0.0
    return glow_gain_in * (glow_mid / yc_in - 0.5)

def smoothstep01(x):
    t = min(max(x, 0.0), 1.0)
    return t * t * (3.0 - 2.0 * t)

def lerp(a, b, s):
    return a + s * (b - a)

def saturate(x):
    return min(max(x, 0.0), 1.0)

# ---------------------------------------------------------------------------
# FilmToneMap (TonemapCommon.ush:110-225), fp32-clean transcription
# ---------------------------------------------------------------------------

def film_tone_map(color_ap1):
    with np.errstate(divide="ignore", invalid="ignore"):
        color_ap0 = AP1_2_AP0 @ color_ap1

        saturation = rgb_2_saturation(color_ap0)
        yc_in = rgb_2_yc(color_ap0)
        s = sigmoid_shaper((saturation - 0.4) / 0.2)
        added_glow = 1.0 + glow_fwd(yc_in, RRT_GLOW_GAIN * s, RRT_GLOW_MID)
        color_ap0 = color_ap0 * added_glow

        hue = rgb_2_hue(color_ap0)
        centered_hue = center_hue(hue, RRT_RED_HUE)
        hue_weight = smoothstep01(1.0 - abs(2.0 * centered_hue / RRT_RED_WIDTH)) ** 2
        color_ap0[0] += (hue_weight * saturation *
                         (RRT_RED_PIVOT - color_ap0[0]) * (1.0 - RRT_RED_SCALE))

        working = AP0_2_AP1 @ color_ap0
        working = np.maximum(working, 0.0)
        working = lerp(np.array(working @ AP1_RGB2Y), working, PRE_DESAT)

        toe_scale = 1.0 + FILM_BLACK_CLIP - FILM_TOE
        shoulder_scale = 1.0 + FILM_WHITE_CLIP - FILM_SHOULDER
        in_match = 0.18
        out_match = 0.18

        if FILM_TOE > 0.8:
            toe_match = (1.0 - FILM_TOE - out_match) / FILM_SLOPE + np.log10(in_match)
        else:
            bt = (out_match + FILM_BLACK_CLIP) / toe_scale - 1.0
            toe_match = (np.log10(in_match)
                         - 0.5 * np.log((1.0 + bt) / (1.0 - bt)) * (toe_scale / FILM_SLOPE))

        straight_match = (1.0 - FILM_TOE) / FILM_SLOPE - toe_match
        shoulder_match = FILM_SHOULDER / FILM_SLOPE - straight_match

        log_color = np.log10(working)
        straight_color = FILM_SLOPE * (log_color + straight_match)
        toe_color = (-FILM_BLACK_CLIP
                     + (2.0 * toe_scale)
                     / (1.0 + np.exp((-2.0 * FILM_SLOPE / toe_scale) * (log_color - toe_match))))
        shoulder_color = ((1.0 + FILM_WHITE_CLIP)
                          - (2.0 * shoulder_scale)
                          / (1.0 + np.exp((2.0 * FILM_SLOPE / shoulder_scale)
                                          * (log_color - shoulder_match))))

        toe_color = np.where(log_color < toe_match, toe_color, straight_color)
        shoulder_color = np.where(log_color > shoulder_match, shoulder_color, straight_color)

        t = np.clip((log_color - toe_match) / (shoulder_match - toe_match), 0.0, 1.0)
        if shoulder_match < toe_match:
            t = 1.0 - t
        t = (3.0 - 2.0 * t) * t * t

        # Guarded blend: identical to UE lerp on UE's (finite) domain, but
        # exact-black inputs (log10(0) = -inf) do not produce inf * 0 = NaN.
        tone = np.where(t <= 0.0, toe_color,
                        np.where(t >= 1.0, shoulder_color,
                                 toe_color + t * (shoulder_color - toe_color)))

        tone = lerp(np.array(tone @ AP1_RGB2Y), tone, POST_DESAT)
        return np.maximum(tone, 0.0)

# ---------------------------------------------------------------------------
# Full frozen chain (docs/postprocess.md)
# ---------------------------------------------------------------------------

def expand_gamut_step(color_ap1):
    luma = float(color_ap1 @ AP1_RGB2Y)
    if luma <= 0.0:
        return color_ap1  # documented guard: UE only evaluates the LUT grid
    chroma = color_ap1 / luma
    chroma_dist_sqr = float((chroma - 1.0) @ (chroma - 1.0))
    expand_amount = ((1.0 - np.exp2(-4.0 * chroma_dist_sqr))
                     * (1.0 - np.exp2(-4.0 * EXPAND_GAMUT * luma * luma)))
    return lerp(color_ap1, EXPAND_MAT @ color_ap1, expand_amount)

def apply_filmic_chain(scene_color_srgb, exposure_ev=0.0):
    exposed = np.asarray(scene_color_srgb, dtype=np.float64) * np.exp2(exposure_ev)
    color_ap1 = TO_AP1 @ exposed
    color_ap1 = expand_gamut_step(color_ap1)
    color_ap1 = lerp(color_ap1, BLUE_CORRECT_AP1 @ color_ap1, BLUE_CORRECTION)
    color_ap1 = film_tone_map(color_ap1)
    color_ap1 = lerp(color_ap1, BLUE_CORRECT_INV_AP1 @ color_ap1, BLUE_CORRECTION)
    return np.maximum(FROM_AP1 @ color_ap1, 0.0)

# ---------------------------------------------------------------------------
# Report
# ---------------------------------------------------------------------------

def fmt_vec(v, digits=9):
    return "(" + ", ".join(f"{x:.{digits}f}" for x in v) + ")"

def print_matrix(name, m):
    print(f"{name} (column convention):")
    for row in m:
        print("  [" + ", ".join(f"{x: .10f}" for x in row) + "]")
    t = m.T
    print(f"{name} TRANSPOSED (RenderLab row-major literal, copy into C++/HLSL):")
    print("  {" + ", ".join(f"{x!r}" for row in t for x in row) + "}")
    print()

def main():
    print("== Composite matrices ==")
    print_matrix("TO_AP1        ", TO_AP1)
    print_matrix("FROM_AP1      ", FROM_AP1)
    print_matrix("BLUE_CORRECT_AP1    ", BLUE_CORRECT_AP1)
    print_matrix("BLUE_CORRECT_INV_AP1", BLUE_CORRECT_INV_AP1)
    print_matrix("EXPAND_MAT    ", EXPAND_MAT)

    print("== Sanity checks ==")
    gray18 = np.array([0.18, 0.18, 0.18])
    out18 = apply_filmic_chain(gray18)
    print(f"chain(0.18 gray)          = {fmt_vec(out18)}   (fixed point expected)")
    out0 = apply_filmic_chain(np.zeros(3))
    print(f"chain(black)              = {fmt_vec(out0)}   (exactly 0 expected)")
    inv_err = np.max(np.abs(BLUE_CORRECT_INV @ BLUE_CORRECT - np.eye(3)))
    print(f"max |BlueCorrectInv*BlueCorrect - I| = {inv_err:.3e}")
    samples = [gray18, np.array([1., 0., 0.]), np.array([0., 0., 1.]), np.array([4., 2., 0.5])]
    rt = [FROM_AP1 @ (TO_AP1 @ c) - c for c in samples]
    print(f"max |FromAP1*ToAP1 - I| on samples  = {np.max(np.abs(rt)):.3e}")
    film18 = film_tone_map(TO_AP1 @ gray18)
    print(f"FilmToneMap(0.18 AP1 neutral)        = {fmt_vec(film18)}   (~0.18)")

    print()
    print("== Reference vectors (float64 chain output; C++ fp32 must match within tolerance) ==")
    battery = [
        ("midgray18",       [0.18, 0.18, 0.18], 0.0),
        ("gray005",         [0.05, 0.05, 0.05], 0.0),
        ("gray1",           [1.0, 1.0, 1.0], 0.0),
        ("gray4",           [4.0, 4.0, 4.0], 0.0),
        ("gray16",          [16.0, 16.0, 16.0], 0.0),
        ("red",             [1.0, 0.0, 0.0], 0.0),
        ("green",           [0.0, 1.0, 0.0], 0.0),
        ("blue",            [0.0, 0.0, 1.0], 0.0),
        ("cyan",            [0.0, 1.0, 1.0], 0.0),
        ("magenta",         [1.0, 0.0, 1.0], 0.0),
        ("yellow",          [1.0, 1.0, 0.0], 0.0),
        ("sceneish",        [0.2126, 0.7152, 0.0722], 0.0),
        ("teal_hdr",        [0.1, 0.4, 0.9], 0.0),
        ("blue_hdr",        [0.1, 0.3, 6.0], 0.0),
        ("orange_hdr",      [8.0, 0.5, 0.1], 0.0),
        ("midgray_ev_plus1",[0.18, 0.18, 0.18], 1.0),
        ("midgray_ev_minus2",[0.18, 0.18, 0.18], -2.0),
        ("teal_ev_plus2",   [0.1, 0.4, 0.9], 2.0),
        ("black",           [0.0, 0.0, 0.0], 0.0),
    ]
    max32 = 0.0
    for name, color, ev in battery:
        out64 = apply_filmic_chain(color, ev)
        # fp32 simulation to size the C++ test tolerance
        with np.errstate(all="ignore"):
            f32 = np.frombuffer(
                apply_filmic_chain(
                    np.asarray(color, dtype=np.float64).astype(np.float32).astype(np.float64),
                    ev).astype(np.float32).tobytes(), dtype=np.float32).astype(np.float64)
        delta = float(np.max(np.abs(out64 - f32)))
        max32 = max(max32, delta)
        print(f"  {name:18s} ev={ev:+.0f} in={fmt_vec(color, 4)} -> out={fmt_vec(out64)}  fp32delta={delta:.2e}")
    print(f"\nmax fp32-vs-fp64 delta over battery: {max32:.3e}")

    print()
    print("== Gray ramp monotonicity (0.001 .. 100) ==")
    prev = -1.0
    ok = True
    for g in [0.001, 0.01, 0.05, 0.1, 0.18, 0.5, 1.0, 2.0, 4.0, 8.0, 16.0, 50.0, 100.0]:
        out = apply_filmic_chain([g, g, g])[1]
        flag = "" if out >= prev else "  <-- NON-MONOTONIC"
        if out < prev:
            ok = False
        print(f"  gray {g:8.3f} -> {out:.9f}{flag}")
        prev = out
    print(f"monotonic: {ok}")

if __name__ == "__main__":
    main()
