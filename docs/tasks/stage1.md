# Stage 1: Reproducible Project Bootstrap

## 1. Repository Skeleton and Minimal Application

- [x] Create the project directories, root `CMakeLists.txt`, and CMake presets.
- [x] Build a dependency-free Win32 empty-window application.
- [x] Verify that both Debug and Release configurations build and run.

## 2. Toolchain and Dependency Locking

- [ ] Record the verified Visual Studio, MSBuild, Windows SDK, and CMake versions.
- [ ] Pin the vcpkg baseline and triplet.
- [ ] Pin the Agility SDK, DXC, and WinPixEventRuntime versions.
- [ ] Complete `docs/build-environment.md` and the dependency manifests.

## 3. Automation

- [ ] Implement `scripts/bootstrap.ps1`.
- [ ] Add the CPU test entry point.
- [ ] Configure Windows GitHub Actions for Debug/Release builds and CPU tests.

## 4. Clean-Environment Verification

- [ ] Remove the build output and repeat bootstrap, configure, build, test, and run.
- [ ] Confirm that no implicit machine dependency or version placeholder remains.

## Next Task

Continue with loop 2: lock the toolchain and initial dependency versions.
