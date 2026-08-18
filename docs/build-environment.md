# Build Environment

This file records the machine that validated implementation step S0.1. It is not a promise
that every later developer has identical versions. Later steps must treat a version change as
a recorded environment difference, not as an unexplained renderer change.

## Validated Host

| Item | Value |
|---|---|
| Recorded on | 2026-08-18 |
| OS | Microsoft Windows 11 Pro |
| OS version | 10.0.26100 |
| Architecture | x64 |
| Primary GPU | NVIDIA GeForce RTX 4070 SUPER |
| GPU driver | 576.88 (`32.0.15.7688`, reported 2025-06-24) |
| Compute capability | 8.9 |
| Secondary display adapter | GameViewer Virtual Display Adapter `15.6.5.199` |

The NVIDIA adapter is the intended validation GPU. The virtual display adapter is present on
this machine and must not be selected as the D3D12 device.

## Validated Toolchain

| Item | Value |
|---|---|
| Visual Studio | Visual Studio Professional 2022 17.12.3 (`17.12.35527.113`) |
| Toolset | `v143` |
| MSVC default toolset file | 14.42.34433 (`cl` 19.42.34435) |
| MSVC used by `windows-vs2022` preset | 14.38.33130 (`cl` 19.38.33143.0) |
| Other installed MSVC toolsets | 14.29.30133, 14.38.33130, 14.42.34433 |
| Windows SDK | 10.0.22621.0 |
| CMake | 3.25.3 |
| Generator used by this repo | `Visual Studio 17 2022` x64 |
| PIX | 2603.25 (`WinPix.exe` 1.0.2603.25001) |
| PIX install path | `C:\Program Files\Microsoft PIX\2603.25` |

`cl.exe` is not on the default PowerShell `PATH`. Use a Visual Studio developer environment or
the repo CMake presets, which already select the VS 2022 x64 generator. The preset pins
toolset `v143` but not a specific MSVC side-by-side version. The S0.1 configure used
14.38.33130. S0.2 should keep using the same preset and record any toolset change.

## Upstream Requirements Compared With This Machine

| Requirement | Upstream source | This machine | Status |
|---|---|---|---|
| Windows x64 or ARM64 | Donut README | Windows 11 x64 | satisfied |
| C++17 compiler, VS 2022 | Donut README | MSVC 19.42 / VS 2022 17.12.3 | satisfied |
| CMake 3.31 | Donut README | CMake 3.25.3 | documented gap |
| `cmake_minimum_required` 3.10 | Donut `CMakeLists.txt` | CMake 3.25.3 | satisfied |
| `cmake_minimum_required` 3.11 | NVRHI `CMakeLists.txt` | CMake 3.25.3 | satisfied |
| `cmake_minimum_required` 3.15 | ShaderMake `CMakeLists.txt` | CMake 3.25.3 | satisfied |
| Windows SDK 10.0.22621.0 or later | NVRHI README | 10.0.22621.0 | satisfied |
| DXC for DX12 | Donut README / ShaderMake | pinned `v1.9.2602` | S0.2 FetchContent |
| PIX for GPU capture | RenderLab Stage 0 | 2603.25 | satisfied |

Donut's README asks for CMake 3.31, but the locked Donut, NVRHI, and ShaderMake trees only
declare 3.10, 3.11, and 3.15. S0.2 configured this Donut revision successfully with CMake
3.25.3. Keep that minimum until a later step proves 3.31 is required.

## Shader Compilation Path

Donut does not compile HLSL through Visual Studio shader items. The locked path is:

1. CMake adds Donut's `ShaderMake` subdirectory.
2. ShaderMake downloads the pinned DXC zip `dxc_2026_02_20.zip` from
   `https://github.com/microsoft/DirectXShaderCompiler/releases/download/v1.9.2602/dxc_2026_02_20.zip`.
3. `SHADERMAKE_DXC_PATH` points at `bin/x64/dxc.exe` inside that extract.
4. `donut_compile_shaders()` / `donut_compile_shaders_all_platforms()` invoke ShaderMake.
5. For D3D12, ShaderMake emits DXIL with default shader model `6_5`. RenderLab can pass
   `SHADER_MODEL 6_6` when its own shaders need Shader Model 6.6.

FXC, Slang, and Vulkan DXC are not part of the S0.1 baseline.

## D3D12 / DXR Capability On This Host

The locked NVRHI D3D12 backend reports:

- `Feature::RayTracingAccelStruct` and `Feature::RayTracingPipeline` when
  `D3D12_RAYTRACING_TIER_1_0` is present.
- `Feature::RayQuery` when `D3D12_RAYTRACING_TIER_1_1` is present.

An RTX 4070 SUPER with driver 576.88 is expected to expose DXR 1.1. S0.3 must print the
adapter name, driver version when available, NVRHI backend, DXR tier, and shader model.
Unsupported DXR hardware must still start the raster path.

## PIX

PIX 2603.25 is installed and will be used for the S0.5 capture checklist. Do not commit
capture files. WinPixEventRuntime is not a locked source dependency in S0.1; Donut/NVRHI
markers are enough until S0.5 decides otherwise.

## Commands Used To Record This File

```powershell
cmake --version
& "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products *
Get-Content "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\Microsoft.VCToolsVersion.default.txt"
& "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Tools\MSVC\14.42.34433\bin\Hostx64\x64\cl.exe"
Get-ChildItem "C:\Program Files (x86)\Windows Kits\10\Include"
nvidia-smi --query-gpu=name,driver_version,compute_cap --format=csv
Get-ItemProperty "HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion"
Get-ChildItem "C:\Program Files\Microsoft PIX"
```
