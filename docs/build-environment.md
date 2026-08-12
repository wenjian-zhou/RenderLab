# Build Environment

RenderLab uses CMake to generate a Visual Studio 2022 x64 solution. Visual Studio uses MSBuild
for compilation and provides the native editing and debugging workflow.

## Verified Toolchain

| Component | Verified version |
|---|---|
| Visual Studio 2022 Professional | 17.12.3 |
| MSVC compiler | 19.38.33143.0 |
| MSVC toolset | 14.38.33130 |
| MSBuild | Included with Visual Studio 2022 |
| Windows SDK | 10.0.22621.0 |
| CMake | 3.25.3 |

Visual Studio must include the **Desktop development with C++** workload.

## Generate the Solution

From PowerShell:

```powershell
.\build.bat
```

From Command Prompt:

```bat
build.bat
```

The script only configures CMake and generates this solution; it does not compile or test:

```text
out/build/windows-vs2022/RenderLab.sln
```

Open the solution in Visual Studio and select Debug or Release with the x64 platform.

## Optional Command-line Build and Test

The CMake build and test presets remain available for automation and clean-environment checks:

```bat
cmake --build --preset windows-debug
ctest --preset windows-debug

cmake --build --preset windows-release
ctest --preset windows-release
```

The linker output must use `/machine:x64`.
