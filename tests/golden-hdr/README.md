# S2.4 HDR Golden

This directory contains the approved S2.4 HDR regression baseline.
The `.rlhdr`, Reinhard `lighting-lit.png` thumbnail, and adjacent
`hdr-capture-metadata.json` are review artifacts; do not replace them from an
unverified run.

[`manifest.json`](manifest.json) is a descriptive, machine-readable summary of
the baseline. The comparator's rules are implemented in `tests/hdr_compare.cpp`.

Generated captures and reports belong under the gitignored `/results/`
directory. The locked inputs, tolerances, portability policy, limitations, and
approval commands are defined in the canonical
[`docs/hdr-regression.md`](../../docs/hdr-regression.md) contract.
