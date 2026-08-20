# S1.6 Golden Images

This directory holds the first image-regression baseline. Captures are the S1.5
visualized GBuffer channels (`DecodeGBuffer` + ADR-002 debug modes), not lighting
and not the S3 tone map.

## Locked capture

| Input | Value |
|---|---|
| Scene | `cesium-milk-truck` |
| Camera | `s04-default` with `--lock-camera` |
| Resolution | 1280×720 |
| Frame | 1 |
| Sample count | 1 |

Do not change the S0.4 camera to make metallic more interesting. Milk Truck
metallic is 0; `gbuffer-metallic.png` is a **weak oracle** (near-black). It
still fails a channel swap onto a non-black view.

## Files

Approved images live in `cesium-milk-truck/s04-default/1280x720/`:

- `gbuffer-base-color.png`
- `gbuffer-world-normal.png`
- `gbuffer-roughness.png`
- `gbuffer-metallic.png`
- `gbuffer-ao-flags.png`
- `gbuffer-linear-depth.png`
- `capture-metadata.json`

Those PNG names are the S1.5 `--dump-gbuffer-views` names. `--output` reuses
that dump and adds `capture-metadata.json`.

Generated re-captures and comparison reports go under `/results/` and are
gitignored. Do not commit `results/` or `captures/`.

## Capture and compare

```powershell
.\out\build\windows-vs2022\bin\Debug\RenderLab.exe --headless --lock-camera --output results\s16-run1
.\out\build\windows-vs2022\bin\Debug\RenderLabGoldenCompare.exe --candidate results\s16-run1 --reference tests\golden\cesium-milk-truck\s04-default\1280x720
powershell -NoProfile -File scripts\golden.ps1
```

`scripts\golden.ps1` captures twice, compares both runs, and proves that
feeding roughness as base-color fails.

GitHub-hosted `windows-2022` has no NVIDIA GPU. CI runs the CPU comparison
tests (self-compare of these files, synthetic channel swap). GPU capture
remains a local or self-hosted test, same as `scripts\smoke.ps1`.

Comparison rules are documented in [`docs/image-regression.md`](../../docs/image-regression.md)
and implemented in `tests/image_compare.cpp`. They are not exact PNG byte
equality.
