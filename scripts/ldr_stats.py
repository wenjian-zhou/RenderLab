#!/usr/bin/env python3
"""LDR capture evidence tool (S3.2).

Prints luminance statistics for tone-mapped final.png captures and, when given
two or more captures in exposure order, the adjacent-EV brightness ratios over
the mid/low-luminance region (mask: linear Y < 0.5 in the lower capture).

This is evidence for docs/PROGRESS.md (the --exposure-ev sweep and the
highlight-rolloff check), not a gate: docs/postprocess.md section 9 keeps
golden-hdr.ps1 as the regression oracle. The exact 2^EV relation is proven on
CPU by tests/test_postprocess_contract.cpp; the GPU-side check is that the
tone-mapped output responds monotonically and near-linearly per stop while
highlights keep gradation instead of clipping to a flat white.

Usage:
  python scripts/ldr_stats.py <capture-dir-or-png> [<capture-dir-or-png> ...]

Each argument is a --output-hdr capture directory (final.png is used) or a
direct PNG path. Pass them in ascending exposure order to get the ratios.
"""

import argparse
from pathlib import Path

import numpy as np
from PIL import Image


def resolve_capture(name):
    path = Path(name)
    if path.is_dir():
        path = path / "final.png"
    if not path.is_file():
        raise SystemExit(f"error: '{name}' is not a PNG file or a directory containing final.png")
    return path


def load_srgb_and_linear_luma(path):
    image = Image.open(path).convert("RGB")
    srgb = np.asarray(image, dtype=np.float64) / 255.0
    # IEC 61966-2-1 sRGB decode: the inverse of the hardware OETF applied on
    # store to the SRGBA8_UNORM target (docs/postprocess.md section 5).
    linear = np.where(srgb <= 0.04045, srgb / 12.92, ((srgb + 0.055) / 1.055) ** 2.4)
    luma = 0.2126 * linear[..., 0] + 0.7152 * linear[..., 1] + 0.0722 * linear[..., 2]
    return srgb, luma


def capture_stats(path):
    srgb, luma = load_srgb_and_linear_luma(path)
    white = np.all(srgb >= 254.0 / 255.0, axis=-1)
    bright_non_white = (luma >= 0.5) & ~white
    return {
        "path": path,
        "luma": luma,
        "mean": float(luma.mean()),
        "median": float(np.median(luma)),
        "p90": float(np.percentile(luma, 90)),
        "max": float(luma.max()),
        "white_fraction": float(white.mean()),
        "bright_non_white_fraction": float(bright_non_white.mean()),
    }


def main():
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument(
        "captures",
        nargs="+",
        help="capture directories (or final.png paths), in ascending exposure order")
    args = parser.parse_args()

    entries = [capture_stats(resolve_capture(name)) for name in args.captures]

    header = (f"{'capture':<44} {'meanY':>10} {'medianY':>10} {'p90Y':>10} "
              f"{'maxY':>8} {'white%':>8} {'brightNW%':>10}")
    print(header)
    print("-" * len(header))
    for entry in entries:
        print(f"{str(entry['path']):<44} {entry['mean']:10.6f} {entry['median']:10.6f} "
              f"{entry['p90']:10.6f} {entry['max']:8.4f} "
              f"{100 * entry['white_fraction']:8.4f} {100 * entry['bright_non_white_fraction']:10.4f}")

    means = [entry["mean"] for entry in entries]
    medians = [entry["median"] for entry in entries]
    # Non-decreasing, not strictly increasing: a black scene background keeps the
    # median at 0 across the whole sweep.
    print(f"\nnon-decreasing mean/median across captures: "
          f"{all(a <= b for a, b in zip(means, means[1:])) and all(a <= b for a, b in zip(medians, medians[1:]))}")

    if len(entries) > 1:
        print("\nadjacent-EV mid/low-luminance ratios (mask: lower capture linear Y < 0.5):")
        for lower, upper in zip(entries, entries[1:]):
            mask = lower["luma"] < 0.5
            if int(mask.sum()) == 0:
                print(f"  {lower['path'].parent.name} -> {upper['path'].parent.name}: no mid/low pixels")
                continue
            ratio = float(upper["luma"][mask].mean() / lower["luma"][mask].mean())
            print(f"  {lower['path'].parent.name} -> {upper['path'].parent.name}: "
                  f"mean ratio = {ratio:.4f} over {int(mask.sum())} px")


if __name__ == "__main__":
    main()
