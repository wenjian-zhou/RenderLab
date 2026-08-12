# Build Environment

RenderLab uses CMake to generate a Visual Studio 2022 x64 solution. This file is the source of
truth for the minimum verified native toolchain and the initial dependency lock. Versions are
exact unless a row explicitly says "or newer."

## Verified Toolchain

The following combination was verified on 2026-08-12:

| Component | Minimum verified version |
|---|---|
| Visual Studio 2022 | 17.12.3 (17.12.35527.113) or newer |
| MSVC compiler | 19.38.33143 or newer within v143 |
| MSVC v143 toolset | 14.38.33130 or newer v143 |
| MSBuild | 17.12.12.57101 or newer |
| Windows SDK | 10.0.22621.0 or newer |
| CMake | 3.25.3 or newer |
| Git for Windows | 2.39.2.windows.1 or newer |
| vcpkg tool (repository bootstrap) | 2026-07-27-98d7cb0cf1f4686a3e43aa5672b6230c1d56bce8 |

Visual Studio must include **Desktop development with C++**. The CMake preset selects the
Visual Studio 17 2022 generator and x64 architecture. The linker output must use `/machine:x64`.

Rows marked "or newer" are minimum constraints; rows without that marker are exact locks. When a
minimum or exact lock changes, update this table and repeat Debug and Release clean-environment
verification.

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

The baseline is the dereferenced commit of the official stable vcpkg `2026.07.29` release. The
bootstrap clones that exact tag into `external/vcpkg/`, verifies its peeled commit, and builds the
vcpkg tool version declared by the locked checkout. A Visual Studio-bundled or PATH vcpkg client is
not used.

The overlay triplet fixes `VCPKG_TARGET_ARCHITECTURE=x64`, `VCPKG_LIBRARY_LINKAGE=static`,
`VCPKG_CRT_LINKAGE=dynamic`, and the v143 toolset family. The initial local baseline was verified
with toolset `14.38.33130`, but newer v143 toolsets are supported and selected normally by Visual
Studio. Every vcpkg target and host dependency in a build uses this one triplet; do not mix
artifacts from `x64-windows`, `x64-windows-static`, or another installation.

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

`scripts/bootstrap.ps1` consumes this lock, installs packages below `external/nuget/`, validates
their nuspec versions and required headers/libraries/DLLs, checks binary versions, and generates
`external/versions.json` with SHA-256 hashes. Existing valid packages are reused; an unexpected or
modified package directory causes bootstrap to fail without overwriting it. Package directories,
the vcpkg checkout, downloads, registries, binary caches, and build artifacts are ignored by Git.

The generated version attestation records both the vcpkg ports baseline and the separately
versioned vcpkg executable. The DXC NuGet package version and its PE binary version intentionally
differ; both values are recorded and validated according to their published version formats.

## D3D12 Header Ownership

Header ownership is intentionally split by purpose and must not vary by developer machine:

- `d3d12.h`, `d3d12sdklayers.h`, and their matching ABI declarations come from the locked
  Agility SDK package.
- `d3dx12.h` comes from the `directx-headers` vcpkg port at the locked baseline.
- Windows SDK or copied sample versions of these headers must not appear earlier on the include
  path. Do not commit a private copy of `d3dx12.h`.

This policy prevents an unrecorded Windows SDK header or a stale helper header from silently
changing the D3D12 interface seen by the application.

The CMake integration is centralized in `cmake/AgilitySDK.cmake`, `cmake/DXC.cmake`, and
`cmake/WinPixEventRuntime.cmake`. Agility headers precede system include paths. Builds stage
`D3D12Core.dll` and `d3d12SDKLayers.dll` under `bin/<configuration>/D3D12/`, and place
`dxcompiler.dll`, `dxil.dll`, and `WinPixEventRuntime.dll` beside the executable.

## Verified Development Machine

| Component | Recorded value |
|---|---|
| OS | Windows 11 x64 |
| GPU | NVIDIA GeForce RTX 4070 SUPER |
| Display driver | 32.0.15.7688 (NVIDIA 576.88), dated 2025-06-24 |

This loop verifies the native toolchain and dependency contracts only. DXR tier, Shader Model,
Agility runtime loading, and GPU validation are recorded when the D3D12 device bootstrap exists;
they are not inferred from the adapter name.

## Bootstrap Dependencies

Run bootstrap once after cloning and whenever either dependency manifest changes:

```powershell
.\scripts\bootstrap.ps1
```

The script requires Git and Windows PowerShell, downloads the fixed NuGet CLI, materializes the
three locked Microsoft packages, and installs the vcpkg manifest using only
`x64-windows-static-md`. All vcpkg downloads, registries, and binary-cache traffic is redirected to
ignored directories below `out/`; user-wide vcpkg caches are not build inputs.

Bootstrap is idempotent. A repeated successful run revalidates all files and leaves
`external/versions.json` byte-for-byte unchanged.

## Generate the Solution

From PowerShell or Command Prompt:

```bat
build.bat
```

The script configures CMake and generates, but does not compile, this solution. Bootstrap must
have completed first:

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

To run only the formal CPU test entry point used by CI, add `--label-regex cpu` to either `ctest`
command. The unfiltered presets also run the Win32 window-lifecycle smoke test.

The preset fixes the generator, x64 architecture, MSVC v143 toolset family, vcpkg toolchain,
install root, overlay triplet, and repository-local cache paths. Visual Studio selects an installed
Windows SDK; CMake rejects SDKs older than `10.0.22621.0`, MSVC older than 19.38, a non-v143
toolset, or a different target or host triplet.

`.github/workflows/windows.yml` performs bootstrap and configure on `windows-2022`, then builds and
runs CPU tests for both Debug and Release. The checkout action is pinned to an immutable commit;
the preset validates the runner's v143 compiler family and minimum compiler/SDK versions.
