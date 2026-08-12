# RenderLab

RenderLab is a Windows rendering laboratory focused on D3D12, DXR, and reproducible graphics
experiments.

## Requirements

- Visual Studio 2022 with **Desktop development with C++**
- CMake 3.25 or newer
- Git for Windows
- Windows PowerShell
- Windows SDK 10.0.22621.0 or newer

## Generate the Visual Studio Solution

From PowerShell:

```powershell
.\scripts\bootstrap.ps1
.\build.bat
```

The script generates, but does not build, the solution at:

```text
out/build/windows-vs2022/RenderLab.sln
```

Open the solution in Visual Studio and use the Debug or Release x64 configuration. See
[Build Environment](docs/build-environment.md) for command-line build and test instructions.

Command-line verification is also available:

```powershell
cmake --build --preset windows-debug
ctest --preset windows-debug
cmake --build --preset windows-release
ctest --preset windows-release
```

The implementation roadmap is maintained in [PLAN.md](PLAN.md).
