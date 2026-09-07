#!/usr/bin/env python3
"""End-to-end GPU-vs-reference verification for the S3.2 tone map (review tool).

Loads a committed/captured hdr-scene-color.rlhdr, evaluates the independent
float64 reference chain (postprocess_reference.apply_filmic_chain) at the
capture's exposureEV, replicates the D3D12 store conversion for an
R8G8B8A8_UNORM_SRGB target (clamp linear to [0,1] -> IEC 61966-2-1 sRGB OETF
-> round to nearest, ties to even), and compares the result pixel-by-pixel
against the capture's final.png.

This closes the loop that the S3.1 CPU battery opened: it proves the GPU
pipeline (exposure position, matrix orientation and apply order, curve,
hardware OETF) against an implementation transcribed from UE 5.8.1 sources,
not against the shipped HLSL. Evidence tool, not a gate: golden-hdr.ps1 is the
regression oracle.

Usage:
  python scripts/verify_final_vs_reference.py <capture-dir> [--sample N]
"""

import argparse
import json
import sys
from pathlib import Path

import numpy as np
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
import postprocess_reference as ref  # noqa: E402


def load_rlhdr_rgb(path):
    raw = Path(path).read_bytes()
    if raw[0:6] != b"RLHDR1":
        raise SystemExit(f"error: {path} is not an RLHDR1 file")
    width = int.from_bytes(raw[12:16], "little")
    height = int.from_bytes(raw[16:20], "little")
    fmt = int.from_bytes(raw[20:24], "little")
    if fmt != 1:
        raise SystemExit(f"error: {path} format {fmt} is not RGBA16_FLOAT")
    expected = 32 + width * height * 8
    if len(raw) != expected:
        raise SystemExit(f"error: {path} size {len(raw)} != expected {expected}")
    halves = np.frombuffer(raw, dtype=np.float16, count=width * height * 4, offset=32)
    rgb = halves.reshape(height, width, 4)[:, :, :3].astype(np.float64)
    return rgb


def d3d_srgb_store(linear_rgb):
    # D3D12 store to R8G8B8A8_UNORM_SRGB: clamp the linear color to [0,1],
    # apply the IEC 61966-2-1 OETF, quantize 8-bit round-to-nearest (ties even).
    c = np.clip(linear_rgb, 0.0, 1.0)
    encoded = np.where(c <= 0.0031308, 12.92 * c, 1.055 * np.power(c, 1.0 / 2.4) - 0.055)
    return np.clip(np.rint(encoded * 255.0), 0.0, 255.0)


def main():
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("capture_dir", help="--output-hdr capture directory")
    parser.add_argument("--sample", type=int, default=0,
                        help="verify only every Nth pixel (0 = all pixels)")
    args = parser.parse_args()

    capture = Path(args.capture_dir)
    metadata = json.loads((capture / "hdr-capture-metadata.json").read_text())
    exposure_ev = float(metadata["exposureEV"])

    scene = load_rlhdr_rgb(capture / "hdr-scene-color.rlhdr")
    final = np.asarray(Image.open(capture / "final.png").convert("RGB"), dtype=np.float64)

    height, width, _ = scene.shape
    if final.shape != scene.shape:
        raise SystemExit(f"error: final.png {final.shape} != rlhdr {scene.shape}")

    stride = max(1, args.sample)
    ys, xs = np.mgrid[0:height, 0:width]
    mask = (ys % stride == 0) & (xs % stride == 0)
    coords = np.argwhere(mask)
    print(f"capture={capture}  {width}x{height}  exposureEV={exposure_ev:g}  "
          f"pixels checked={len(coords)}")

    worst = 0.0
    diff_counts = {1: 0, 2: 0, 3: 0}
    mismatch_at_worst = None
    for y, x in coords:
        expected = d3d_srgb_store(ref.apply_filmic_chain(scene[y, x], exposure_ev))
        actual = final[y, x]
        diff = np.abs(expected - actual)
        pixel_worst = float(diff.max())
        if pixel_worst > worst:
            worst = pixel_worst
            mismatch_at_worst = (int(y), int(x), expected.tolist(), actual.tolist(),
                                  scene[y, x].tolist())
        for threshold in diff_counts:
            if pixel_worst >= threshold:
                diff_counts[threshold] += 1

    checked = len(coords)
    print(f"max |8-bit diff| = {worst:.0f}")
    for threshold in sorted(diff_counts):
        count = diff_counts[threshold]
        print(f"pixels with any channel diff >= {threshold}: {count} "
              f"({100.0 * count / checked:.4f}%)")
    if mismatch_at_worst:
        y, x, expected, actual, scene_rgb = mismatch_at_worst
        print(f"worst pixel at (x={x}, y={y}): scene={scene_rgb} "
              f"expected={expected} actual={actual}")

    verdict = "PASS" if worst <= 1.0 else "FAIL"
    print(f"verdict: {verdict} (tolerance: max diff <= 1 8-bit unit, "
          f"expected only store-rounding noise)")
    return 0 if verdict == "PASS" else 1


if __name__ == "__main__":
    raise SystemExit(main())
