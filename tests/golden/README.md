# S1.6 Golden Images

This directory contains the approved S1.6 GBuffer image-regression baseline.
The PNGs and adjacent `capture-metadata.json` are review artifacts; do not
replace them from an unverified run.

[`manifest.json`](manifest.json) is a descriptive, machine-readable summary of
the baseline. The comparator's rules are implemented in `tests/image_compare.cpp`.

Generated captures and reports belong under the gitignored `/results/`
directory. The locked inputs, tolerances, portability policy, limitations, and
approval commands are defined in the canonical
[`docs/image-regression.md`](../../docs/image-regression.md) contract.
