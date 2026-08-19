# Donut NVRHI Rendering Lab

RenderLab is a Windows real-time rendering architecture project built on NVIDIA Donut and
NVRHI. Its two primary learning deliverables are a small, explainable render dependency graph
and a scene-level DXR architecture. The raster pipeline is built first so both systems evolve
inside a working renderer instead of as isolated experiments.

## Repository State

The repository was reset on 2026-08-18 after retiring the original from-scratch D3D12 route.
The final legacy snapshot is preserved on branch `backup/legacy-d3d12-20260818` at commit
`856b4c2`.

This branch is a Donut/NVRHI planning baseline. **S0.5 / M0 is complete** and **S1.1 is
complete**: renderer conventions and the first GBuffer contract are frozen. The application
loads the fixed Cesium Milk Truck glTF scene through Donut, presents a stable clear + UI
frame, emits stable CPU/GPU markers, and can be captured with PIX. Device, queue, fence, and
swap-chain ownership stay in `donut::app::DeviceManager`. Geometry is loaded but not drawn.
The next executable task is **S1.2: define frame, view, instance, and material data**.

## Plans

- [Revised project direction](NEW_PLAN.md) is the original Chinese design brief.
- [Detailed implementation plan](IMPLEMENTATION_PLAN.md) is the authoritative English execution
  plan, including step order, deliverables, verification, and exit gates.
- [Progress tracker](docs/PROGRESS.md) records the current step and evidence as work proceeds.
- [Upstream lock file](dependencies.lock.json) pins Donut `bfdebdd7dd5455c503b2737a1967a4ef651c145b`
  and NVRHI `8e8c36e37558acec333204619b95d9d2fcdc4a79`.
- [Build environment](docs/build-environment.md) records the validated Windows toolchain.
- [Capture guide](docs/capture-guide.md) is the M0 PIX checklist. Do not commit capture files.
- [Renderer conventions](docs/renderer-conventions.md) freeze handedness, matrices, reversed-Z, and color space.
- [GBuffer contract](docs/g-buffer.md) is the first-version target list, formats, and clears.
- [ADR-001](docs/adr/ADR-001-donut-nvrhi-baseline.md) explains the baseline and acquisition method.
- [ADR-002](docs/adr/ADR-002-gbuffer-layout.md) records the GBuffer format decision.

If these documents disagree, `IMPLEMENTATION_PLAN.md` controls execution scope, while
`NEW_PLAN.md` controls high-level intent.

## Bootstrap and Configure

Requirements:

- Windows 11
- Visual Studio 2022 with **Desktop development with C++**
- MSVC 19.38 or newer
- CMake 3.25 or newer
- Git, with recursive submodule support

A clean checkout must initialize Donut and its recursive submodules before CMake runs:

```powershell
powershell -NoProfile -File scripts\bootstrap.ps1
```

That script is equivalent to `git submodule update --init --recursive`, then checks every
locked Git commit in `dependencies.lock.json`.

Generate the Visual Studio 2022 x64 solution:

```powershell
.\build.bat
```

Or configure and build it directly:

```powershell
cmake --fresh --preset windows-vs2022
cmake --build --preset windows-debug
cmake --build --preset windows-release
```

CMake enables only the D3D12 Donut/NVRHI backend. DX11, Vulkan, Streamline, DLSS, Aftermath,
and RTXMU stay off.

## Run

After a Debug or Release build:

```powershell
.\out\build\windows-vs2022\bin\Debug\RenderLab.exe --lock-camera
.\out\build\windows-vs2022\bin\Release\RenderLab.exe --lock-camera --frames 30
.\out\build\windows-vs2022\bin\Debug\RenderLab.exe --headless
.\out\build\windows-vs2022\bin\Debug\RenderLab.exe --scene fallback-boxes --lock-camera --frames 15
powershell -NoProfile -File scripts\smoke.ps1
```

The process opens a window, loads the default scene from `scenes/`, applies the S0.4 camera
preset, clears the back buffer, draws the diagnostics panel, and presents through Donut.
Startup prints the selected adapter, driver version when DXGI exposes it, NVRHI backend,
validation mode, DXR tier, shader model, stable marker names, scene inventory, and camera
preset. Hardware without DXR still starts the raster path.

`--headless` is the CI-safe smoke mode: the window is hidden, the camera is locked, and the
process presents 8 frames by default. GitHub-hosted runners do not have the required NVIDIA
GPU, so CI stays configure + build and smoke is a documented local test. See
[`docs/capture-guide.md`](docs/capture-guide.md) for the M0 PIX checklist.

Command-line parsing is stable:

| Option | S0.5 behavior |
|---|---|
| `--help` | print usage and exit |
| `--frames <n>` | present `n` frames, then exit |
| `--scene <id\|path>` | load a scene id or a path relative to `scenes/` |
| `--lock-camera` | disable free-camera motion and keep the S0.4 preset |
| `--headless` | hidden-window fixed-frame smoke; locks the camera |
| `--output <path>` | parsed; reports `not implemented` until S1.6 |
| `--dx12` / `--d3d12` | accepted no-ops; D3D12 is the only backend |

The default scene is `cesium-milk-truck`. A tiny committed fallback is available with
`--scene fallback-boxes`. Scene URLs, licenses, and SHA-256 hashes are recorded in
[`scenes/manifest.json`](scenes/manifest.json). Absolute developer-machine paths are rejected.
