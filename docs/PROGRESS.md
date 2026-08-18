# Progress Tracker

This file records evidence for the active implementation step. The detailed scope and exit gates
live in [`../IMPLEMENTATION_PLAN.md`](../IMPLEMENTATION_PLAN.md).

## Current State

- Active step: **S0.2 - Add Donut as the pinned external dependency**
- State: **Not started**
- Last updated: 2026-08-18
- Current branch: `main`
- Legacy snapshot: `backup/legacy-d3d12-20260818` at `856b4c2`

## Completed Repository Reset

- Archived all prior uncommitted work in commit `856b4c2`.
- Created the `backup/legacy-d3d12-20260818` recovery branch.
- Removed the retired from-scratch Win32/D3D12 runtime, its tests, and its stage documents from the
  new main-line working tree.
- Retained the license and reusable repository/build conventions.
- Replaced the old application build with a configuration-only planning baseline.

## Completed Steps

### S0.1 - Select and record the upstream baseline

```text
Step: S0.1
State: Complete
Date: 2026-08-18
Commit: 1300e76835bd2d93ee33c437eaaad8d91825554e
Commands:
  git status; git log --oneline -20; git branch -a
  cmake --version
  vswhere -latest -products *
  cl.exe 19.42.34435 (toolset 14.42.34433)
  nvidia-smi --query-gpu=name,driver_version,compute_cap --format=csv
  git clone --no-checkout https://github.com/NVIDIA-RTX/Donut.git
  git checkout --detach bfdebdd7dd5455c503b2737a1967a4ef651c145b
  git submodule update --init --recursive
  git submodule status --recursive
  GitHub API inspection of NVIDIA-RTX/Donut, NVRHI, ShaderMake, Donut-Samples
  Resolve microsoft/DirectX-Headers tag v1.717.0-preview
  Download and SHA-256 dxc_2026_02_20.zip
  cmake --fresh --preset windows-vs2022
Automated tests: not applicable; S0.1 is a selection and lock step
GPU validation/capture: not applicable
Artifacts:
  dependencies.lock.json
  docs/build-environment.md
  docs/adr/ADR-001-donut-nvrhi-baseline.md
  THIRD_PARTY_NOTICES.md
  docs/PROGRESS.md
  README.md
Known limitations:
  Donut README asks for CMake 3.31; this machine validated CMake 3.25.3.
  DirectX-Headers is locked to the commit that currently backs v1.717.0-preview.
  Agility SDK, Streamline, DLSS, Vulkan, and RTXMU are deferred.
  Donut submodule is intentionally not added; that is S0.2.
Next step: S0.2 - Add Donut as the pinned external dependency
```

Selected baseline:

- Donut `bfdebdd7dd5455c503b2737a1967a4ef651c145b`
- NVRHI `8e8c36e37558acec333204619b95d9d2fcdc4a79`
- Acquisition: recursive Git submodules; no vcpkg
- Application reference: `donut::app::ApplicationBase` + `donut::app::DeviceManager`,
  following Donut-Samples `examples/basic_triangle`

## Step Evidence Template

Copy this section when completing a step:

```text
Step:
State:
Date:
Commit:
Commands:
Automated tests:
GPU validation/capture:
Artifacts:
Known limitations:
Next step:
```
