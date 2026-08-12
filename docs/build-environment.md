# Build Environment

RenderLab uses CMake to generate a Visual Studio 2022 x64 solution. This file is the source of
truth for the minimum verified native toolchain and the initial dependency lock. Versions are
exact unless a row explicitly says "or newer."

## Verified Toolchain

The following combination was verified on 2026-08-12:

| Component | Minimum verified version |
|---|---|
| Visual Studio 2022 Professional | 17.12.3 (17.12.35527.113) |
| MSVC compiler | 19.38.33143 |
| MSVC v143 toolset | 14.38.33130 |
| MSBuild | 17.12.12.57101 |
| Windows SDK | 10.0.22621.0 |
| CMake | 3.25.3 |
| Git for Windows | 2.39.2.windows.1 |
| vcpkg client (Visual Studio bundle) | 2024-09-30-ab8988503c7cffabfd440b243a383c0a352a023d |

Visual Studio must include **Desktop development with C++**. The CMake preset selects the
Visual Studio 17 2022 generator and x64 architecture. The linker output must use `/machine:x64`.

Newer toolchains are not assumed to be compatible merely because they are installed. When the
minimum is raised, update this table and repeat Debug and Release clean-environment verification.

## vcpkg Lock

RenderLab uses vcpkg manifest mode with these fixed inputs:

| Setting | Locked value |
|---|---|
| Registry | `https://github.com/microsoft/vcpkg` |
| Release | `2026.07.29` |
| `builtin-baseline` | `9e593bb18ea69cc5095e012465dcd675a822ed0d` |
| Target triplet | `x64-windows-static-md` |
| Host triplet | `x64-windows-static-md` |
| Overlay triplet | `triplets/x64-windows-static-md.cmake` |

The baseline is the dereferenced commit of the official stable vcpkg `2026.07.29` release. It is
deliberately independent of the locally installed vcpkg client's own build commit shown above.

The overlay triplet fixes `VCPKG_TARGET_ARCHITECTURE=x64`, `VCPKG_LIBRARY_LINKAGE=static`,
`VCPKG_CRT_LINKAGE=dynamic`, and the verified v143 toolset version `14.38.33130`. Every vcpkg
target and host dependency in a build uses this one triplet; do not mix artifacts from
`x64-windows`, `x64-windows-static`, or another installation.

`vcpkg.json` is authoritative for the baseline and the initial v0.1 C++ dependency set:

- Catch2, CLI11, D3D12 Memory Allocator, DirectXMath, DirectXTex, fastgltf, fmt, and
  nlohmann/json.
- `directx-headers`, used only as the repository's source of `d3dx12.h` and its helper API.

The baseline resolves the exact port versions. Dependency upgrades must change the baseline in a
dedicated commit and pass the complete Debug/Release and CPU-test regression suite.

## Microsoft Runtime Package Lock

`dependencies.lock.json` is the machine-readable source for Microsoft package acquisition. It
contains no floating versions.

| Component | Package ID | Locked version |
|---|---|---|
| NuGet CLI | `nuget.exe` | 6.14.0 |
| D3D12 Agility SDK | `Microsoft.Direct3D.D3D12` | 1.619.5 |
| DirectX Shader Compiler | `Microsoft.Direct3D.DXC` | 1.9.2607.13 |
| PIX event runtime | `WinPixEventRuntime` | 1.0.240308001 |

The automation loop will make `scripts/bootstrap.ps1` consume this lock, install packages below
`external/nuget/`, validate their required headers/libraries/DLLs, and generate
`external/versions.json`. Package directories and generated vcpkg artifacts are ignored by Git.

## D3D12 Header Ownership

Header ownership is intentionally split by purpose and must not vary by developer machine:

- `d3d12.h`, `d3d12sdklayers.h`, and their matching ABI declarations come from the locked
  Agility SDK package.
- `d3dx12.h` comes from the `directx-headers` vcpkg port at the locked baseline.
- Windows SDK or copied sample versions of these headers must not appear earlier on the include
  path. Do not commit a private copy of `d3dx12.h`.

This policy prevents an unrecorded Windows SDK header or a stale helper header from silently
changing the D3D12 interface seen by the application.

## Verified Development Machine

| Component | Recorded value |
|---|---|
| OS | Windows 11 x64 |
| GPU | NVIDIA GeForce RTX 4070 SUPER |
| Display driver | 32.0.15.7688 (NVIDIA 576.88), dated 2025-06-24 |

This loop verifies the native toolchain and dependency contracts only. DXR tier, Shader Model,
Agility runtime loading, and GPU validation are recorded when the D3D12 device bootstrap exists;
they are not inferred from the adapter name.

## Generate the Solution

From PowerShell or Command Prompt:

```bat
build.bat
```

The script configures CMake and generates, but does not compile, this solution:

```text
out/build/windows-vs2022/RenderLab.sln
```

Open the solution in Visual Studio and select Debug or Release with the x64 platform.

The build and test presets remain available for automation and clean-environment checks:

```bat
cmake --build --preset windows-debug
ctest --preset windows-debug

cmake --build --preset windows-release
ctest --preset windows-release
```

Until `scripts/bootstrap.ps1` is added in the automation loop, dependency materialization is a
separate vcpkg step. The current Win32 bootstrap target remains dependency-free.
