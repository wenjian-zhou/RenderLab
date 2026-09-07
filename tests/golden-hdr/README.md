# S2.4 / S3.2 HDR + Tone-Mapped Golden

This directory contains the approved HDR regression baseline. The `.rlhdr`
(pre-exposure `HDRSceneColor`), the Reinhard `lighting-lit.png` thumbnail, the
S3.2 tone-mapped `final.png`, and the adjacent `hdr-capture-metadata.json` are
review artifacts; do not replace them from an unverified run.

The S3.2 re-baseline (schema v2, `final.png`, `exposureEV` pinned at 0) was
minted on the same adapter that now owns the S2.4 baseline content: the
re-captured `.rlhdr` is byte-identical to the S2.4 original.

[`manifest.json`](manifest.json) is a descriptive, machine-readable summary of
the baseline. The comparator's rules are implemented in `tests/hdr_compare.cpp`.

Generated captures and reports belong under the gitignored `/results/`
directory. The locked inputs, tolerances, portability policy, limitations, and
approval commands are defined in the canonical
[`docs/hdr-regression.md`](../../docs/hdr-regression.md) contract.
