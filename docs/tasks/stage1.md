# Stage 1: Reproducible Project Bootstrap

## 1. Repository Skeleton and Minimal Application

- [x] Create the project directories, root `CMakeLists.txt`, and CMake presets.
- [x] Build a dependency-free Win32 empty-window application.
- [x] Verify that both Debug and Release configurations build and run.

## 2. Toolchain and Dependency Locking

- [x] Record the verified Visual Studio, MSBuild, Windows SDK, and CMake versions.
- [x] Pin the vcpkg baseline and triplet.
- [x] Pin the Agility SDK, DXC, and WinPixEventRuntime versions.
- [x] Complete `docs/build-environment.md` and the dependency manifests.

## 3. Automation

- [x] Implement `scripts/bootstrap.ps1`.
- [x] Add the CPU test entry point.
- [x] Configure Windows GitHub Actions for Debug/Release builds and CPU tests.

## 4. Clean-Environment Verification

- [x] Remove the build output and repeat bootstrap, configure, build, test, and run.
- [x] Confirm that no implicit machine dependency or version placeholder remains.

Clean-environment verification completed on 2026-08-12: generated dependency and build directories
were removed, bootstrap downloaded the locked inputs successfully, and a repeated bootstrap left
`external/versions.json` byte-for-byte unchanged. `build.bat`, Debug/Release builds, the formal CPU
test entry point, and the Win32 window-lifecycle smoke test all passed.

## Next Task

Stage 1 is complete. Continue with Stage 2: D3D12 Core and the three-frame lifetime model.
