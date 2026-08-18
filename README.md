# Donut NVRHI Rendering Lab

RenderLab is a Windows real-time rendering architecture project built on NVIDIA Donut and
NVRHI. Its two primary learning deliverables are a small, explainable render dependency graph
and a scene-level DXR architecture. The raster pipeline is built first so both systems evolve
inside a working renderer instead of as isolated experiments.

## Repository State

The repository was reset on 2026-08-18 after retiring the original from-scratch D3D12 route.
The final legacy snapshot is preserved on branch `backup/legacy-d3d12-20260818` at commit
`856b4c2`.

This branch is a Donut/NVRHI planning baseline. **S0.3 is complete**: the application target
subclasses `donut::app::ApplicationBase` and lets `donut::app::DeviceManager` own the window,
D3D12 device, queues, fences, and swap chain. The next executable task is **S0.4: load one
fixed scene and camera**.

## Plans

- [Revised project direction](NEW_PLAN.md) is the original Chinese design brief.
- [Detailed implementation plan](IMPLEMENTATION_PLAN.md) is the authoritative English execution
  plan, including step order, deliverables, verification, and exit gates.
- [Progress tracker](docs/PROGRESS.md) records the current step and evidence as work proceeds.
- [Upstream lock file](dependencies.lock.json) pins Donut `bfdebdd7dd5455c503b2737a1967a4ef651c145b`
  and NVRHI `8e8c36e37558acec333204619b95d9d2fcdc4a79`.
- [Build environment](docs/build-environment.md) records the validated Windows toolchain.
- [ADR-001](docs/adr/ADR-001-donut-nvrhi-baseline.md) explains the baseline and acquisition method.

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
.\out\build\windows-vs2022\bin\Debug\RenderLab.exe
.\out\build\windows-vs2022\bin\Release\RenderLab.exe --frames 30
```

The process opens a window, clears the back buffer, draws the baseline ImGui panel, and
presents through Donut. Startup prints the selected adapter, driver version when DXGI
exposes it, NVRHI backend, DXR tier, and shader model. Hardware without DXR still starts
the raster path.

Command-line parsing is stable:

| Option | S0.3 behavior |
|---|---|
| `--help` | print usage and exit |
| `--frames <n>` | present `n` frames, then exit |
| `--scene <path>` | parsed; reports `not implemented` until S0.4 |
| `--headless` | parsed; reports `not implemented` until S0.5 |
| `--output <path>` | parsed; reports `not implemented` until S0.5 |
| `--dx12` / `--d3d12` | accepted no-ops; D3D12 is the only backend |
