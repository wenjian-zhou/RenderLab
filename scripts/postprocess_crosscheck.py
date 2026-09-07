#!/usr/bin/env python3
"""Cross-check the S3.1 frozen post-process literals against UE 5.8.1 sources.

Parses the matrix and scalar literals out of three places and compares them:

  1. UE 5.8.1 sources (ground truth, parsed live - not transcribed by hand):
       Engine/Shaders/Private/ACES/ACESCommon.ush
       Engine/Shaders/Private/PostProcessCombineLUTs.usf
       Engine/Shaders/Private/TonemapCommon.ush
       Engine/Source/Runtime/Engine/Private/Scene.cpp
  2. src/renderer/PostProcessContract.h  (C++ mirror)
  3. src/shaders/postprocess.hlsli       (HLSL mirror)

Rules verified:
  - C++ literal == HLSL literal (exact float equality) for every matrix and scalar.
  - C++ matrix literal == UE matrix literal TRANSPOSED (RenderLab row-vector
    convention, docs/renderer-conventions.md section 2).
  - AP1_RGB2Y is a vector and is not transposed.
  - Frozen scalar constants match the UE defaults parsed from Scene.cpp and
    TonemapCommon.ush.

Usage: python scripts/postprocess_crosscheck.py [path-to-UE-source]
       (default UE path: G:\\UnrealEngine)

Exits non-zero on any mismatch.
"""

import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent


def read(path: Path) -> str:
    text = path.read_text(encoding="utf-8", errors="replace")
    return re.sub(r"//[^\n]*", "", text)  # strip line comments (they contain digits)


def parse_floats(text: str) -> list:
    return [float(x) for x in re.findall(r"[-+]?\d+\.\d+(?:[eE][-+]?\d+)?|[-+]?\d+(?:[eE][-+]?\d+)?", text)]


def parse_matrices(text: str) -> dict:
    out = {}
    for m in re.finditer(r"float3x3\s+(\w+)\s*=\s*\{([^}]*)\}", text):
        values = parse_floats(m.group(2))
        if len(values) == 9:
            out[m.group(1)] = values
    return out


def parse_cpp_matrices(text: str) -> dict:
    out = {}
    for m in re.finditer(r"float3x3\s+k(\w+)\s*=\s*donut::math::float3x3\(([^)]*)\)", text):
        values = parse_floats(m.group(2))
        if len(values) == 9:
            out["k" + m.group(1)] = values
    return out


def parse_scalar(text: str, name: str, kind="const"):
    if kind == "const":
        m = re.search(rf"\b{name}\s*=\s*([-\d.eE+]+)", text)
    else:  # C++ member assignment like "FilmSlope = 0.88f;"
        m = re.search(rf"\b{name}\s*=\s*([-\d.eE+]+)f?\s*;", text)
    return float(m.group(1)) if m else None


def transpose9(m):
    return [m[0], m[3], m[6], m[1], m[4], m[7], m[2], m[5], m[8]]


def main():
    ue_root = Path(sys.argv[1] if len(sys.argv) > 1 else r"G:\UnrealEngine")
    failures = []

    aces_path = ue_root / "Engine/Shaders/Private/ACES/ACESCommon.ush"
    combine_path = ue_root / "Engine/Shaders/Private/PostProcessCombineLUTs.usf"
    tonemap_path = ue_root / "Engine/Shaders/Private/TonemapCommon.ush"
    scene_path = ue_root / "Engine/Source/Runtime/Engine/Private/Scene.cpp"
    for p in (aces_path, combine_path, tonemap_path, scene_path):
        if not p.is_file():
            print(f"UE source not found: {p}")
            return 1

    cpp_path = REPO / "src/renderer/PostProcessContract.h"
    hlsl_path = REPO / "src/shaders/postprocess.hlsli"
    aces = read(aces_path)
    combine = read(combine_path)
    tonemap = read(tonemap_path)
    scene = read(scene_path)
    cpp = read(cpp_path)
    hlsl = read(hlsl_path)

    ue_mats = parse_matrices(aces)
    ue_mats.update({k: v for k, v in parse_matrices(combine).items()
                    if k in ("BlueCorrect", "BlueCorrectInv", "Wide_2_XYZ_MAT")})
    cpp_mats = parse_cpp_matrices(cpp)
    hlsl_mats = parse_matrices(hlsl)

    # UE matrix name -> RenderLab constant name (literals transposed).
    matrix_pairs = [
        ("sRGB_2_XYZ_MAT", "kSRGBToXYZ"),
        ("XYZ_2_sRGB_MAT", "kXYZToSRGB"),
        ("AP1_2_XYZ_MAT", "kAP1ToXYZ"),
        ("XYZ_2_AP1_MAT", "kXYZToAP1"),
        ("AP0_2_AP1_MAT", "kAP0ToAP1"),
        ("AP1_2_AP0_MAT", "kAP1ToAP0"),
        ("D65_2_D60_CAT", "kD65ToD60CAT"),
        ("D60_2_D65_CAT", "kD60ToD65CAT"),
        ("BlueCorrect", "kBlueCorrect"),
        ("BlueCorrectInv", "kBlueCorrectInv"),
        ("Wide_2_XYZ_MAT", "kWideToXYZ"),
    ]

    print(f"Cross-checking {len(matrix_pairs)} matrices + AP1_RGB2Y against {ue_root}")
    for ue_name, our_name in matrix_pairs:
        ue_t = transpose9(ue_mats[ue_name])
        ours_cpp = cpp_mats.get(our_name)
        ours_hlsl = hlsl_mats.get(our_name)
        if ours_cpp is None or ours_hlsl is None:
            failures.append(f"{our_name}: missing in {'C++' if ours_cpp is None else 'HLSL'}")
            continue
        if ours_cpp != ours_hlsl:
            failures.append(f"{our_name}: C++ and HLSL literals differ\n"
                            f"    cpp  {ours_cpp}\n    hlsl {ours_hlsl}")
        if ours_cpp != ue_t:
            failures.append(f"{our_name}: literal != UE {ue_name} transposed\n"
                            f"    ours {ours_cpp}\n    UE^T {ue_t}")
        else:
            print(f"  PASS  {our_name} == UE {ue_name} transposed (C++ == HLSL)")

    # AP1_RGB2Y vector (not transposed).
    m = re.search(r"float3\s+AP1_RGB2Y\s*=\s*\{([^}]*)\}", aces)
    ue_y = parse_floats(m.group(1))
    cpp_y = parse_floats(re.search(
        r"kAP1RGB2Y\s*=\s*donut::math::float3\(([^)]*)\)", cpp).group(1))
    hlsl_y = parse_floats(re.search(r"float3\s+kAP1RGB2Y\s*=\s*\{([^}]*)\}", hlsl).group(1))
    if cpp_y == hlsl_y == ue_y:
        print("  PASS  kAP1RGB2Y == UE AP1_RGB2Y (C++ == HLSL)")
    else:
        failures.append(f"kAP1RGB2Y: cpp {cpp_y} hlsl {hlsl_y} UE {ue_y}")

    # Scalar constants: (RenderLab name, UE expected parsed from source, UE source text).
    scalar_checks = [
        ("kFilmSlope", parse_scalar(scene, "FilmSlope", "assign"), "Scene.cpp FilmSlope"),
        ("kFilmToe", parse_scalar(scene, "FilmToe", "assign"), "Scene.cpp FilmToe"),
        ("kFilmShoulder", parse_scalar(scene, "FilmShoulder", "assign"), "Scene.cpp FilmShoulder"),
        ("kFilmBlackClip", parse_scalar(scene, "FilmBlackClip", "assign"), "Scene.cpp FilmBlackClip"),
        ("kFilmWhiteClip", parse_scalar(scene, "FilmWhiteClip", "assign"), "Scene.cpp FilmWhiteClip"),
        ("kBlueCorrectionAmount", parse_scalar(scene, "BlueCorrection", "assign"), "Scene.cpp BlueCorrection"),
        ("kExpandGamutAmount", parse_scalar(scene, "ExpandGamut", "assign"), "Scene.cpp ExpandGamut"),
        ("kRRTGlowGain", parse_scalar(tonemap, "RRT_GLOW_GAIN"), "TonemapCommon.ush RRT_GLOW_GAIN"),
        ("kRRTGlowMid", parse_scalar(tonemap, "RRT_GLOW_MID"), "TonemapCommon.ush RRT_GLOW_MID"),
        ("kRRTRedScale", parse_scalar(tonemap, "RRT_RED_SCALE"), "TonemapCommon.ush RRT_RED_SCALE"),
        ("kRRTRedPivot", parse_scalar(tonemap, "RRT_RED_PIVOT"), "TonemapCommon.ush RRT_RED_PIVOT"),
        ("kRRTRedHue", parse_scalar(tonemap, "RRT_RED_HUE"), "TonemapCommon.ush RRT_RED_HUE"),
        ("kRRTRedWidth", parse_scalar(tonemap, "RRT_RED_WIDTH"), "TonemapCommon.ush RRT_RED_WIDTH"),
        ("kFilmInMatch", parse_scalar(tonemap, "InMatch"), "TonemapCommon.ush InMatch"),
        ("kFilmOutMatch", parse_scalar(tonemap, "OutMatch"), "TonemapCommon.ush OutMatch"),
        ("kYcRadiusWeight", parse_scalar(aces, "ycRadiusWeight"), "ACESCommon.ush rgb_2_yc default"),
    ]
    # Desaturation amounts are inline literals in UE's lerp calls.
    scalar_checks += [
        ("kPreDesaturate", parse_floats(re.search(r"lerp\(\s*dot\(\s*WorkingColor[^)]*\)[^;]*?,\s*WorkingColor,\s*([\d.]+)\s*\)", tonemap).group(1))[0],
         "TonemapCommon.ush pre-desaturate"),
        ("kPostDesaturate", parse_floats(re.search(r"lerp\(\s*dot\(\s*float3\(ToneColor\)[^)]*\)[^;]*?,\s*ToneColor,\s*([\d.]+)\s*\)", tonemap).group(1))[0],
         "TonemapCommon.ush post-desaturate"),
    ]

    for name, ue_value, source in scalar_checks:
        cpp_value = parse_scalar(cpp, name)
        hlsl_value = parse_scalar(hlsl, name)
        if ue_value is None:
            failures.append(f"{name}: could not parse UE value from {source}")
            continue
        if cpp_value != ue_value or hlsl_value != ue_value:
            failures.append(f"{name}: cpp {cpp_value} hlsl {hlsl_value} UE {ue_value} ({source})")
        else:
            print(f"  PASS  {name} = {ue_value:g} ({source})")

    if failures:
        print(f"\n{len(failures)} FAILURE(S):")
        for f in failures:
            print(f"  FAIL  {f}")
        return 1
    print("\nAll literals match UE 5.8.1 (matrices transposed for the row-vector convention).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
