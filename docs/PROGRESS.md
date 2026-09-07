# Progress Tracker

This file records evidence for the active implementation step. The detailed scope and exit gates
live in [`../IMPLEMENTATION_PLAN.md`](../IMPLEMENTATION_PLAN.md).

## Current State

- Active step: **S3.3 - Freeze the manual-pipeline reference**
- State: **S3.2 complete**
- Last updated: 2026-09-07
- Current branch: `main`
- Legacy snapshot: `backup/legacy-d3d12-20260818` at `856b4c2`
- Stage 0 gate: **M0 satisfied**
- Stage 1: **S1.1 through S1.6 complete**; Stage 1 gate satisfied
- Stage 2: **S2.1 through S2.4 complete**; Stage 2 gate satisfied
- Stage 3: **S3.1 and S3.2 complete** (tone mapping + present landed; the M1
  reference freeze is S3.3)

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
Commit: 3b6b86c8fd0937662ee3a4bffa3b8a0d1c42144a
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

### S0.2 - Add Donut as the pinned external dependency

```text
Step: S0.2
State: Complete
Date: 2026-08-18
Commit: 9d98dbbef57c4a8e5a0466d4ec0e114185795e8b
Commands:
  git submodule add https://github.com/NVIDIA-RTX/Donut.git external/donut
  git -C external/donut checkout --detach bfdebdd7dd5455c503b2737a1967a4ef651c145b
  git submodule update --init --recursive -- external/donut
  git submodule status --recursive
  powershell -NoProfile -File scripts\bootstrap.ps1 -SkipUpdate
  cmake --fresh --preset windows-vs2022
  cmake --build --preset windows-debug --target ZERO_CHECK
  cmake --build --preset windows-release --target ZERO_CHECK
Automated tests: not applicable; S0.2 is a dependency-integration step
GPU validation/capture: not applicable
Artifacts:
  .gitmodules
  external/donut (gitlink bfdebdd7dd5455c503b2737a1967a4ef651c145b)
  cmake/Donut.cmake
  scripts/bootstrap.ps1
  .github/workflows/windows.yml
  CMakeLists.txt
  CMakePresets.json
  build.bat
  README.md
  THIRD_PARTY_NOTICES.md
  docs/PROGRESS.md
  docs/build-environment.md
Known limitations:
  CMake 3.25.3 configured this Donut revision; the README 3.31 request remains unused.
  DirectX-Headers and DXC are fetched into the CMake build tree, not the Git work tree.
  Donut core/engine/render/app targets stay EXCLUDE_FROM_ALL until S0.3 links them.
  The RenderLab application target remains disabled.
Next step: S0.3 - Create the minimal RenderLab application
```

### S0.3 - Create the minimal RenderLab application

```text
Step: S0.3
State: Complete
Date: 2026-08-18
Commit: eeeadaba5b96319086226e00b93b5eba93f12e9a
Commands:
  cmake --fresh --preset windows-vs2022
  cmake --build --preset windows-debug --parallel
  cmake --build --preset windows-release --parallel
  .\out\build\windows-vs2022\bin\Debug\RenderLab.exe --help
  .\out\build\windows-vs2022\bin\Debug\RenderLab.exe --scene dummy.gltf
  .\out\build\windows-vs2022\bin\Debug\RenderLab.exe --headless
  .\out\build\windows-vs2022\bin\Debug\RenderLab.exe --output out.png
  .\out\build\windows-vs2022\bin\Debug\RenderLab.exe --frames 30 --dx12
  .\out\build\windows-vs2022\bin\Release\RenderLab.exe --frames 30
  60-second Debug windowed run, then WM_CLOSE
Automated tests: CLI parse/help/unimplemented-option checks; --frames 30 Debug and Release smoke
GPU validation/capture:
  Debug 30-frame run: NVIDIA GeForce RTX 4070 SUPER, driver 32.0.15.7688, NVRHI D3D12, DXR 1.1, SM 6.7
  Debug and Release --frames 30 resized 1280x720 -> 1344x784 and exited 0
  Debug 60-second run printed the same capability report, resized to 1280x720, and closed with no NVRHI or D3D12 error
Artifacts:
  src/app/RenderingLabApp.h
  src/app/RenderingLabApp.cpp
  src/app/main.cpp
  src/CMakeLists.txt
  cmake/Donut.cmake
  CMakeLists.txt
  CMakePresets.json
  build.bat
  README.md
  docs/PROGRESS.md
  docs/build-environment.md
  .github/workflows/windows.yml
  .gitignore
Known limitations:
  --scene is parsed and rejected until S0.4
  --headless and --output are parsed and rejected until S0.5
  --frames is implemented so smoke and resize can exit without a second runtime
  DXGI enumerated the NVIDIA adapter twice; software/WARP adapters are skipped
  No scene, GBuffer, RDG, or DXR pass is implemented
Next step: S0.4 - Load one fixed scene and camera
```

### S0.4 - Load one fixed scene and camera

```text
Step: S0.4
State: Complete
Date: 2026-08-18
Commit: ce08881324721424881809eef39155cb46c2ce5d
Commands:
  cmake --build --preset windows-debug --parallel
  cmake --build --preset windows-release --parallel
  .\out\build\windows-vs2022\bin\Debug\RenderLab.exe --help
  .\out\build\windows-vs2022\bin\Debug\RenderLab.exe --headless
  .\out\build\windows-vs2022\bin\Debug\RenderLab.exe --output out.png
  .\out\build\windows-vs2022\bin\Debug\RenderLab.exe --scene <absolute-path>
  .\out\build\windows-vs2022\bin\Debug\RenderLab.exe --scene does-not-exist.gltf
  .\out\build\windows-vs2022\bin\Debug\RenderLab.exe --lock-camera --frames 30
  .\out\build\windows-vs2022\bin\Release\RenderLab.exe --lock-camera --frames 30
  .\out\build\windows-vs2022\bin\Debug\RenderLab.exe --scene fallback-boxes --lock-camera --frames 15
  Flip one byte in the copied Debug CesiumMilkTruck.glb, rerun --lock-camera --frames 5, restore
Automated tests: CLI help/unimplemented options; --scene id, relative path, absolute-path rejection, missing asset, SHA-256 mismatch; locked-camera identity across two Debug restarts and Release
GPU validation/capture:
  Debug and Release --lock-camera --frames 30: NVIDIA GeForce RTX 4070 SUPER, driver 32.0.15.7688, NVRHI D3D12, DXR 1.1, SM 6.7
  Default scene '/scenes/cesium-milk-truck/CesiumMilkTruck.glb': meshes=2 instances=3 materials=4
  Camera preset 's04-default': position=(4.800, 2.400, 5.600) target=(0.000, 0.850, 0.000) up=(0.000, 1.000, 0.000) vfov=45.000 deg znear=0.100 locked=true
  Debug and Release camera lines were byte-identical after restart
  Debug --frames 30 resized 1280x720 -> 1344x784 and exited 0 with no NVRHI or D3D12 error
  Fallback '/scenes/fallback/boxes.gltf': meshes=3 instances=3 materials=3
  Corrupt glb reported SHA-256 mismatch and exited 1 without an absolute path
Artifacts:
  scenes/manifest.json
  scenes/README.md
  scenes/cesium-milk-truck/CesiumMilkTruck.glb
  scenes/cesium-milk-truck/LICENSE.md
  scenes/cesium-milk-truck/README.md
  scenes/fallback/boxes.gltf
  scenes/fallback/boxes.bin
  scenes/fallback/LICENSE.md
  src/app/SceneCatalog.h
  src/app/SceneCatalog.cpp
  src/app/RenderingLabApp.h
  src/app/RenderingLabApp.cpp
  src/app/main.cpp
  src/CMakeLists.txt
  README.md
  THIRD_PARTY_NOTICES.md
  docs/PROGRESS.md
Known limitations:
  --headless and --output remain parsed and rejected until S0.5
  Geometry is loaded and GPU buffers are uploaded, but no mesh is drawn; GBuffer starts in S1.4
  The default scene includes a Cesium trademark/logo; attribution is recorded and the logo is not used as a project mark
  Scene animations are not played, so later image tests stay on a static pose
Next step: S0.5 - Establish observability and capture
```

### S0.5 - Establish observability and capture

```text
Step: S0.5
State: Complete
Date: 2026-08-18
Commit: 87ee4c99d8209ef3bad7f66355a22716e73e8d8a
Commands:
  cmake --build --preset windows-debug --parallel
  cmake --build --preset windows-release --parallel
  .\out\build\windows-vs2022\bin\Debug\RenderLab.exe --help
  .\out\build\windows-vs2022\bin\Debug\RenderLab.exe --output out.png
  .\out\build\windows-vs2022\bin\Debug\RenderLab.exe --headless
  .\out\build\windows-vs2022\bin\Release\RenderLab.exe --headless --frames 8
  .\out\build\windows-vs2022\bin\Debug\RenderLab.exe --lock-camera --frames 30
  .\out\build\windows-vs2022\bin\Release\RenderLab.exe --lock-camera --frames 30
  powershell -NoProfile -File scripts\smoke.ps1
  pixtool launch RenderLab.exe --command-line="--lock-camera --frames 90" take-capture save-capture captures\s05-m0-debug.wpix
  pixtool open-capture captures\s05-m0-debug.wpix save-event-list captures\s05-m0-debug-events.csv
  pixtool launch Release\RenderLab.exe --command-line="--lock-camera --frames 90" take-capture save-capture captures\s05-m0-release.wpix
Automated tests: CLI help; --output remains unimplemented (S1.6); Debug and Release --headless smoke; Debug and Release --lock-camera --frames 30; scripts\smoke.ps1
GPU validation/capture:
  Debug: NVIDIA GeForce RTX 4070 SUPER, driver 32.0.15.7688, NVRHI D3D12, validation=NVRHI + D3D12 debug runtime, DXR 1.1, SM 6.7
  Release: same adapter/driver, validation=NVRHI
  Marker line identical in Debug and Release: Frame, SceneUpdate, Render, UI, Present
  Debug and Release --headless: 8 frames, errors=0
  Debug and Release --lock-camera --frames 30: resized 1280x720 -> 1344x784, errors=0
  PIX 2603.25 GPU captures of Debug and Release show the same readable hierarchy:
    Frame / SceneUpdate / Render / Frame / SceneUpdate / Render / UI / ImGUI / Present
  Capture files were not committed
Artifacts:
  src/app/FrameMarkers.h
  src/app/FrameMarkers.cpp
  src/app/RenderingLabApp.h
  src/app/RenderingLabApp.cpp
  src/app/main.cpp
  src/CMakeLists.txt
  docs/capture-guide.md
  scripts/smoke.ps1
  .github/workflows/windows.yml
  .gitignore
  README.md
  docs/build-environment.md
  docs/PROGRESS.md
Known limitations:
  Meshes are still not drawn; GBuffer starts in S1.4
  --output remains unimplemented until S1.6 screenshot / image regression
  --headless hides the Donut window; it is not a swap-chain-free device
  GitHub-hosted windows-2022 has no NVIDIA GPU, so CI smoke skips and stays a local test
  PIX prefixes Windows SDK pix.h events with a deprecation note; the stable names after the prefix are unchanged
  WinPixEventRuntime / pix3.h was not added as a dependency
Next step: S1.1 - Freeze renderer conventions and the GBuffer contract
```

### S1.1 - Freeze renderer conventions and the GBuffer contract

```text
Step: S1.1
State: Complete
Date: 2026-08-19
Commit: 265c47ae63a341d38bc5bca6ecc221f03bcf3cca
Commands:
  git status; git log --oneline -20
  Read IMPLEMENTATION_PLAN.md S1.1, NEW_PLAN.md 5.1, ADR-001, Donut GBuffer/camera/projection, NVRHI dxgi-format.cpp
  $vcvars = "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat"
  cmd /c "`"$vcvars`" && cl.exe /nologo /EHsc /std:c++20 /O2 /Fo.\out\tmp-s11\ /Fe:.\out\tmp-s11\check-gbuffer-formats.exe scripts\check-gbuffer-formats.cpp /link /nologo && .\out\tmp-s11\check-gbuffer-formats.exe"
Automated tests: D3D12 format-support probe for SRGBA8_UNORM, RGBA16_FLOAT, RGBA8_UNORM, D32_FLOAT DSV, R32_FLOAT SRV, and typeless depth+SRV creation
GPU validation/capture:
  NVIDIA GeForce RTX 4070 SUPER, feature level 12_2, driver 32.0.15.7688
  All required RTV/SRV/DSV bits present; R32_TYPELESS depth with D32_FLOAT DSV + R32_FLOAT SRV created
  No GBuffer textures, shaders, or mesh drawing were added
Artifacts:
  docs/renderer-conventions.md
  docs/g-buffer.md
  docs/adr/ADR-002-gbuffer-layout.md
  scripts/check-gbuffer-formats.cpp
  README.md
  docs/PROGRESS.md
Known limitations:
  CPU/HLSL structs are specified in docs; S1.2 adds the real headers and layout asserts
  GBuffer textures are not created; S1.3 owns lifetime and resize
  Meshes are still not drawn; S1.4 writes the GBuffer
  Velocity, emissive, MSAA, and octahedral normals are intentionally absent
  Lighting units and HDR background color wait for S2.1
  Format probe is a local D3D12 tool, not a CMake target
Next step: S1.2 - Define frame, view, instance, and material data
```

### S1.2 - Define frame, view, instance, and material data

```text
Step: S1.2
State: Complete
Date: 2026-08-19
Commit: 1209f2c62ccc18dac775457f1ceb45cdbc20c768
Commands:
  cmake --preset windows-vs2022
  cmake --build --preset windows-debug --parallel
  cmake --build --preset windows-release --parallel
  .\out\build\windows-vs2022\bin\Debug\RenderLabDataContractTests.exe
  .\out\build\windows-vs2022\bin\Release\RenderLabDataContractTests.exe
  .\out\build\windows-vs2022\bin\Debug\RenderLab.exe --scene fallback-boxes --lock-camera --frames 8
  .\out\build\windows-vs2022\bin\Debug\RenderLab.exe --lock-camera --frames 8
  .\out\build\windows-vs2022\bin\Release\RenderLab.exe --lock-camera --frames 8
Automated tests: RenderLabDataContractTests Debug and Release
  layout sizes/offsets for Frame/View/Instance/Material
  GBuffer flag pack/unpack
  two metal-rough materials (BlueDielectric vs GoldMetal) convert to distinct params
  missing optional textures use documented fallbacks; AO = 1
  conversion is deterministic
  specular-gloss and alpha-tested domains are rejected and logged
  two SceneGraph transforms produce distinct draw records
  FirstPersonCamera view is mirrored; FOV 45 deg is converted to 0.78540 rad
  world-clip-world row-vector round-trip
GPU validation/capture:
  Debug and Release --lock-camera --frames 8: NVIDIA GeForce RTX 4070 SUPER, driver 32.0.15.7688, NVRHI D3D12, DXR 1.1, SM 6.7, errors=0
  Fallback boxes: meshes=3 instances=3 materials=3 opaqueDraws=3 skipped=0
    GroundDielectric / BlueDielectric / GoldMetal are distinct factor-only materials
  Cesium Milk Truck: meshes=2 instances=3 materials=4 opaqueDraws=5 skipped=0
    truck (flags=0x1 textured) vs glass (0.000, 0.041, 0.021) vs window_trim vs wheels
    wheel translations (0.000, 0.428, 1.433) and (0.000, 0.428, -1.352) are distinct
  View: mirrored=true fov=0.78540 rad zNear=0.100 viewport=1280x720
Artifacts:
  src/shaders/renderer_cb.h
  src/shaders/gbuffer_encoding.hlsli
  src/renderer/GBufferContract.h
  src/renderer/RendererData.h
  src/renderer/RendererData.cpp
  tests/test_renderer_data.cpp
  tests/CMakeLists.txt
  src/CMakeLists.txt
  CMakeLists.txt
  src/app/RenderingLabApp.h
  src/app/RenderingLabApp.cpp
  docs/renderer-data.md
  docs/g-buffer.md
  docs/renderer-conventions.md
  README.md
  .github/workflows/windows.yml
  docs/PROGRESS.md
Known limitations:
  GBuffer textures are not created; S1.3 owns lifetime and resize
  Meshes are still not drawn; S1.4 consumes these draw records
  1x1 fallback GPU textures are documented, not uploaded, until S1.4 needs bound SRVs
  Fallback-boxes bakes instance positions into vertices, so node transforms are identity
  Emissive is ignored; it is not a Stage 1 GBuffer channel
  Skinned, alpha-tested, transmissive, and spec-gloss geometry is logged and omitted
Next step: S1.3 - Create persistent GBuffer targets and resize handling
```

### S1.3 - Create persistent GBuffer targets and resize handling

```text
Step: S1.3
State: Complete
Date: 2026-08-19
Commit: a2eb470073020ce3373f2f5fcdef0cd0eb328b2e
Commands:
  cmake --build --preset windows-debug --parallel
  cmake --build --preset windows-release --parallel
  .\out\build\windows-vs2022\bin\Debug\RenderLabDataContractTests.exe
  .\out\build\windows-vs2022\bin\Release\RenderLabDataContractTests.exe
  .\out\build\windows-vs2022\bin\Debug\RenderLab.exe --headless --frames 8
  .\out\build\windows-vs2022\bin\Release\RenderLab.exe --headless --frames 8
  .\out\build\windows-vs2022\bin\Debug\RenderLab.exe --lock-camera --frames 30
  .\out\build\windows-vs2022\bin\Release\RenderLab.exe --lock-camera --frames 30
  powershell -NoProfile -File scripts\smoke.ps1
  pixtool launch Debug\RenderLab.exe --command-line="--lock-camera --frames 20" take-capture save-capture captures\s13-gbuffer-debug.wpix
  pixtool open-capture captures\s13-gbuffer-debug.wpix save-event-list captures\s13-gbuffer-debug-events.csv
  pixtool open-capture captures\s13-gbuffer-debug.wpix export-to-cpp out\tmp-s13-pix
  pixtool launch Release\RenderLab.exe --command-line="--lock-camera --frames 20" take-capture save-capture captures\s13-gbuffer-release.wpix
Automated tests: RenderLabDataContractTests Debug and Release
  existing S1.2 layout/material/draw-record checks
  GBuffer debug names, formats, clears, bytes/pixel
  1280x720 allocation is 18,432,000 bytes
  MakeGBufferTextureDesc matches the frozen contract, including typeless D32 and keepInitialState
  uncreated GBufferTargets exposes null SRV handles
GPU validation/capture:
  Debug: NVIDIA GeForce RTX 4070 SUPER, driver 32.0.15.7688, NVRHI D3D12, validation=NVRHI + D3D12 debug runtime, DXR 1.1, SM 6.7, errors=0
  Release: same adapter/driver, validation=NVRHI, errors=0
  GBuffer created 1280x720 sampleCount=1 mipLevels=1 approxBytes=18432000 A=SRGBA8_UNORM B=RGBA16_FLOAT C=RGBA8_UNORM Depth=D32
  --frames 30 resized 1280x720 -> 1344x784 (approxBytes=21073920, createCount=2) then minimize/restore with no extra recreate and errors=0
  PIX Debug and Release event lists: Render / GBuffer / 3x ClearRenderTargetView / ClearDepthStencilView
  PIX C++ export names and formats at 1280x720, mips=1, samples=1:
    GBufferA R8G8B8A8_UNORM_SRGB ALLOW_RENDER_TARGET clear (0,0,0,1)
    GBufferB R16G16B16A16_FLOAT ALLOW_RENDER_TARGET clear (0,0,0,0)
    GBufferC R8G8B8A8_UNORM ALLOW_RENDER_TARGET clear (0,0,0,1)
    GBufferDepth R32_TYPELESS ALLOW_DEPTH_STENCIL DSV D32_FLOAT clear depth 0
  Capture files were not committed
Artifacts:
  src/renderer/GBufferTargets.h
  src/renderer/GBufferTargets.cpp
  src/app/RenderingLabApp.h
  src/app/RenderingLabApp.cpp
  src/app/main.cpp
  src/app/FrameMarkers.h
  src/CMakeLists.txt
  tests/test_gbuffer_targets.cpp
  tests/CMakeLists.txt
  docs/g-buffer.md
  docs/capture-guide.md
  README.md
  .github/workflows/windows.yml
  docs/PROGRESS.md
Known limitations:
  Meshes are still not drawn; S1.4 writes the GBuffer
  SRV handles are the NVRHI ITexture* objects; lighting bind sets wait for S1.4 / S2
  DeviceManager skip-render while minimized; GBuffer is retained, not destroyed
  --output remains unimplemented until S1.6
  Velocity, emissive, MSAA, and extra targets remain absent
Next step: S1.4 - Implement the opaque GBuffer pass
```

### S1.4 - Implement the opaque GBuffer pass

```text
Step: S1.4
State: Complete
Date: 2026-08-19
Commit: bae4bc60d196cd68aa12b6afad4b5007c57a6a55
Commands:
  cmake --preset windows-vs2022
  cmake --build --preset windows-debug --parallel
  cmake --build --preset windows-release --parallel
  .\out\build\windows-vs2022\bin\Debug\RenderLabDataContractTests.exe
  .\out\build\windows-vs2022\bin\Release\RenderLabDataContractTests.exe
  .\out\build\windows-vs2022\bin\Debug\RenderLab.exe --headless --frames 8
  .\out\build\windows-vs2022\bin\Release\RenderLab.exe --headless --frames 8
  .\out\build\windows-vs2022\bin\Debug\RenderLab.exe --scene fallback-boxes --lock-camera --frames 8
  .\out\build\windows-vs2022\bin\Debug\RenderLab.exe --lock-camera --frames 30
  .\out\build\windows-vs2022\bin\Release\RenderLab.exe --lock-camera --frames 30
  powershell -NoProfile -File scripts\smoke.ps1
  pixtool launch Debug\RenderLab.exe --command-line="--lock-camera --frames 16" take-capture save-capture captures\s14-gbuffer-debug.wpix
  pixtool open-capture captures\s14-gbuffer-debug.wpix save-event-list captures\s14-gbuffer-debug-events.csv
  pixtool open-capture captures\s14-gbuffer-debug.wpix export-to-cpp out\tmp-s14-pix
  pixtool launch Release\RenderLab.exe --command-line="--lock-camera --frames 16" take-capture save-capture captures\s14-gbuffer-release.wpix
Automated tests: RenderLabDataContractTests Debug and Release
  existing S1.2 layout/material/draw-record checks
  existing S1.3 GBuffer target contract checks
  mirrored view frontCounterClockwise
  opaque cull Back; glTF doubleSided cull None
  reversed-Z GreaterOrEqual, depthWrite, depthClipEnable
GPU validation/capture:
  Debug: NVIDIA GeForce RTX 4070 SUPER, driver 32.0.15.7688, NVRHI D3D12, validation=NVRHI + D3D12 debug runtime, DXR 1.1, SM 6.7, errors=0
  Release: same adapter/driver, validation=NVRHI, errors=0
  Cesium Milk Truck: opaqueDraws=5 skipped=0; wheels at (0.000, 0.428, 1.433) and (0.000, 0.428, -1.352)
  Fallback boxes: opaqueDraws=3 skipped=0, dummy TEXCOORD/TANGENT streams
  View: mirrored=true fov=0.78540 rad zNear=0.100 camera preset s04-default
  --frames 30 resized 1280x720 -> 1344x784 then minimize/restore, errors=0
  PIX Debug and Release event lists under Render / GBuffer:
    3x ClearRenderTargetView, ClearDepthStencilView, OMSetRenderTargets(3)+DSV, 5x DrawIndexedInstanced
  PIX C++ export at 1280x720:
    GBufferA R8G8B8A8_UNORM_SRGB clear (0,0,0,1)
    GBufferB R16G16B16A16_FLOAT clear (0,0,0,0)
    GBufferC R8G8B8A8_UNORM clear (0,0,0,1)
    GBufferDepth D32_FLOAT DSV clear depth 0
    DrawIndexedInstanced 5232 / 168 / 864 / 2304 / 2304 matching milk-truck draw records
    EndQuery + ResolveQueryData timestamp scope
  Capture files were not committed
Artifacts:
  src/renderer/GBufferPass.h
  src/renderer/GBufferPass.cpp
  src/shaders/gbuffer_vs.hlsl
  src/shaders/gbuffer_ps.hlsl
  src/shaders/gbuffer_pass.hlsli
  src/shaders/Shaders.cfg
  src/CMakeLists.txt
  src/app/RenderingLabApp.h
  src/app/RenderingLabApp.cpp
  src/app/main.cpp
  tests/test_gbuffer_pass.cpp
  tests/CMakeLists.txt
  docs/g-buffer.md
  docs/capture-guide.md
  docs/renderer-data.md
  docs/renderer-conventions.md
  README.md
  docs/PROGRESS.md
Known limitations:
  Back buffer still presents the existing clear + UI; GBuffer channel views are S1.5
  Image regression is S1.6
  Lighting, tone mapping, RDG, and DXR are not implemented
  Fallback-boxes has no TEXCOORD/TANGENT; dummy zero-UV and (1,0,0,1) tangent streams are bound
  Visual confirmation of camera/instance alignment waits on S1.5 debug views; PIX draw counts and transforms match S1.2 records
Next step: S1.5 - Add GBuffer debug visualization
```

### S1.5 - Add GBuffer debug visualization

```text
Step: S1.5
State: Complete
Date: 2026-08-20
Commit: 5664f3f124a50ed4f7259acaef1fa40c90922255
Commands:
  cmake --build --preset windows-debug --parallel
  cmake --build --preset windows-release --parallel
  .\out\build\windows-vs2022\bin\Debug\RenderLabDataContractTests.exe
  .\out\build\windows-vs2022\bin\Release\RenderLabDataContractTests.exe
  .\out\build\windows-vs2022\bin\Debug\RenderLab.exe --help
  .\out\build\windows-vs2022\bin\Debug\RenderLab.exe --output out.png
  .\out\build\windows-vs2022\bin\Debug\RenderLab.exe --gbuffer-view lighting
  .\out\build\windows-vs2022\bin\Debug\RenderLab.exe --headless --frames 8
  .\out\build\windows-vs2022\bin\Release\RenderLab.exe --headless --frames 8
  .\out\build\windows-vs2022\bin\Debug\RenderLab.exe --lock-camera --dump-gbuffer-views captures\s15-views
  .\out\build\windows-vs2022\bin\Debug\RenderLab.exe --scene fallback-boxes --lock-camera --dump-gbuffer-views captures\s15-fallback-views
  .\out\build\windows-vs2022\bin\Debug\RenderLab.exe --lock-camera --gbuffer-view world-normal --frames 8
  .\out\build\windows-vs2022\bin\Debug\RenderLab.exe --lock-camera --frames 30
  powershell -NoProfile -File scripts\smoke.ps1
  pixtool launch RenderLab.exe --command-line="--lock-camera --frames 16" take-capture save-capture captures\s15-gbuffer-debug.wpix
  pixtool open-capture captures\s15-gbuffer-debug.wpix save-event-list captures\s15-gbuffer-debug-events.csv
Automated tests: RenderLabDataContractTests Debug and Release
  existing S1.2 / S1.3 / S1.4 checks
  six ADR-002 debug modes and channel names
  normal display remap and linearized-depth convention strings
  CLI parse aliases and unknown-mode rejection
  debug raster cull None; depth test/write off
  deviceDepth == 0 rejected; viewZ = zNear / deviceDepth
  reversed-Z linearized viewZ is monotonic and finite
  displayed viewZ/(viewZ+1) stays in (0, 1)
GPU validation/capture:
  Debug: NVIDIA GeForce RTX 4070 SUPER, driver 32.0.15.7688, NVRHI D3D12, validation=NVRHI + D3D12 debug runtime, DXR 1.1, SM 6.7, errors=0
  Release: same adapter/driver, validation=NVRHI, errors=0
  Cesium Milk Truck dumps at 1280x720:
    base color shows the truck, logo, glass, wheels; background is the GBufferA clear
    world normals change with orientation (hood/side/front distinct); background is black, not remapped gray
    roughness is white (scene roughness=1); metallic is black (scene metallic=0)
    AO/flags is yellow (AO=1, ShadingValid=1, TwoSided=0); background is black
    linearized depth is finite gray on the truck; background is magenta and is not reconstructed
  Fallback boxes: roughness 0.90 ground vs 0.75 dielectric; normals vary by face
  --frames 30 resized 1280x720 -> 1344x784 then minimize/restore, errors=0
  PIX event list under Render:
    GBuffer: 3x ClearRenderTargetView, ClearDepthStencilView, 5x DrawIndexedInstanced
    GBufferDebug: OMSetRenderTargets, Barrier, DrawInstanced
  Capture files and PNG dumps were not committed
Artifacts:
  src/renderer/GBufferDebugPass.h
  src/renderer/GBufferDebugPass.cpp
  src/shaders/gbuffer_debug_cb.h
  src/shaders/gbuffer_debug_vs.hlsl
  src/shaders/gbuffer_debug_ps.hlsl
  src/shaders/Shaders.cfg
  src/CMakeLists.txt
  src/app/RenderingLabApp.h
  src/app/RenderingLabApp.cpp
  src/app/main.cpp
  src/app/FrameMarkers.h
  tests/test_gbuffer_debug.cpp
  tests/CMakeLists.txt
  tests/test_renderer_data.cpp
  docs/g-buffer.md
  docs/capture-guide.md
  docs/renderer-conventions.md
  README.md
  IMPLEMENTATION_PLAN.md
  docs/PROGRESS.md
Known limitations:
  Numeric pixel inspection is deferred. Donut PixelReadbackPass maps a GPU readback buffer and stalls; it is bound to one texture at construction and is not a low-cost hover path.
  --output remains unimplemented until S1.6 golden-image regression. --dump-gbuffer-views is a visualization dump only; no tests/golden/, hashes, or comparison rules.
  Milk Truck metallic is 0, so that debug view is black by contract. Fallback GoldMetal is baked off the locked S0.4 look-at and is not required for S1.5.
  viewZ/(viewZ+1) compresses far distances; it stays finite and monotonic.
  Lighting, tone mapping, RDG, and DXR are not implemented.
Next step: S1.6 - Create the first image-regression baseline
```

### S1.6 - Create the first image-regression baseline

```text
Step: S1.6
State: Complete
Date: 2026-08-20
Commit: afecd9a7440b45c6c8620af12be8b8fbfc74c672
Follow-up fixes: a5da94a (required identity fields), bacfb77 (strict JSON parsing)
Commands:
  cmake --build --preset windows-debug --parallel
  cmake --build --preset windows-release --parallel
  .\out\build\windows-vs2022\bin\Debug\RenderLab.exe --headless --lock-camera --output results\s16-approve
  .\out\build\windows-vs2022\bin\Debug\RenderLabDataContractTests.exe
  .\out\build\windows-vs2022\bin\Release\RenderLabDataContractTests.exe
  powershell -NoProfile -File scripts\golden.ps1 -Mode Verify -Configuration Debug
  powershell -NoProfile -File scripts\golden.ps1 -Mode Verify -Configuration Release
Evidence:
  Debug and Release CPU comparison tests passed, including malformed metadata and channel-swap failures
  Committed goldens self-compared successfully; identical Debug and Release recaptures had mae=0
  Debug: NVIDIA GeForce RTX 4070 SUPER, driver 32.0.15.7688, NVRHI D3D12, validation=NVRHI + D3D12 debug runtime, DXR 1.1, SM 6.7, errors=0
  Release: same adapter/driver, validation=NVRHI, errors=0
  Channel swap (roughness vs base-color): mae=15.4302 mismatch=0.113758 maxAbs=255, failed as expected
Contract: docs/image-regression.md
Known limitations:
  Milk Truck metallic is 0, so that golden is a weak oracle. The S0.4 camera was not changed.
  GitHub-hosted windows-2022 has no NVIDIA GPU; CI runs CPU comparison of committed goldens. GPU capture stays a local/self-hosted test.
  The portability band is implemented and classified separately from regressions; it was not exercised on a second GPU.
Next step: S2.1 - Define the lighting contract
```

### S2.1 - Define the lighting contract

```text
Step: S2.1
State: Complete
Date: 2026-08-26
Commit: 3ef36f82ded636ff8064e3bc9166e9d63c8b3cb0
Commands:
  cmake --build --preset windows-debug --parallel --target RenderLabDataContractTests
  cmake --build --preset windows-release --parallel --target RenderLabDataContractTests
  .\out\build\windows-vs2022\bin\Debug\RenderLabDataContractTests.exe
  .\out\build\windows-vs2022\bin\Release\RenderLabDataContractTests.exe
Automated tests: RenderLabDataContractTests Debug and Release
  LightingConstants 320 bytes; directional/point 32 bytes; debug CB 16 bytes
  offsets: toLight@0 intensity@12 color@16 ambient@32 count@44 background@48 flags@60 lights@64
  F0 lerp at metallic 0 / 0.5 / 1; metal has no diffuse albedo
  alpha = max(r^2, 1e-3); attenuation 1/d^2 inside range, 0 at/beyond range
  same d and different ranges match; count 9 clamps to 8
  NDC reconstruction recovers (1,2,3) through the S0.4 camera
  HDRSceneColor desc is RGBA16_FLOAT, RT+SRV, no UAV, clear (0,0,0,1)
GPU validation/capture: not applicable; S2.1 is a contract freeze
Artifacts:
  docs/lighting.md
  src/shaders/lighting_cb.h
  src/shaders/lighting_debug_cb.h
  src/shaders/lighting.hlsli
  src/renderer/LightingContract.h
  tests/test_lighting_contract.cpp
  docs/renderer-conventions.md
  docs/g-buffer.md
  docs/renderer-data.md
  README.md
  IMPLEMENTATION_PLAN.md
  docs/PROGRESS.md
Known limitations:
  No DeferredLightingPass, no lighting pixel shader, no HDR allocation
  Default lights are CPU constants; glTF punctual lights are not imported
  S2.2 creates HDRSceneColor and proves reconstruction on the GPU
Next step: S2.2 - Implement position reconstruction and a diagnostic light
```

### S2.2 - Implement position reconstruction and a diagnostic light

```text
Step: S2.2
State: Complete
Date: 2026-09-05
Commit: 5b53baa74669d49ae38b78a3a5fd955a21a68635
Commands:
  cmake --preset windows-vs2022
  cmake --build --preset windows-debug --parallel
  cmake --build --preset windows-release --parallel
  .\out\build\windows-vs2022\bin\Debug\RenderLabDataContractTests.exe
  .\out\build\windows-vs2022\bin\Release\RenderLabDataContractTests.exe
  .\out\build\windows-vs2022\bin\Debug\RenderLab.exe --help
  .\out\build\windows-vs2022\bin\Debug\RenderLab.exe --headless --frames 8
  .\out\build\windows-vs2022\bin\Debug\RenderLab.exe --lock-camera --frames 30
  .\out\build\windows-vs2022\bin\Debug\RenderLab.exe --lighting-view world-position --lock-camera --frames 8
  .\out\build\windows-vs2022\bin\Debug\RenderLab.exe --gbuffer-view base-color --lock-camera --frames 8
  .\out\build\windows-vs2022\bin\Debug\RenderLab.exe --gbuffer-view base-color --lighting-view ndotl
  .\out\build\windows-vs2022\bin\Debug\RenderLab.exe --lock-camera --dump-lighting-views captures\s22-lighting-views
  powershell -NoProfile -File scripts\smoke.ps1
  powershell -NoProfile -File scripts\golden.ps1 -Mode Verify -Configuration Debug
Automated tests: RenderLabDataContractTests Debug and Release
  existing S1.2 / S1.3 / S1.4 / S1.5 / S1.6 / S2.1 checks
  lighting debug CLI parse (world-position, ndotl, aliases, rejection)
  PresentSource defaults to LightingDebug
  DeferredLighting / LightingDebug raster: cull none, depth test/write off
GPU validation/capture:
  Debug: NVIDIA GeForce RTX 5060 Laptop GPU, driver 32.0.15.7322, NVRHI D3D12,
    validation=NVRHI + D3D12 debug runtime, DXR 1.1, SM 6.7, errors=0
  HDRSceneColor created 1280x720 RGBA16_FLOAT approxBytes=7372800
  Default present is lighting ndotl (no view flags)
  --dump-lighting-views wrote lighting-world-position.png (magenta background + frac(abs(P)))
    and lighting-ndotl.png (grayscale N·L, black background)
  --gbuffer-view and --lighting-view together exit 2
  S1.6 golden.ps1 Verify still passes (mae=0 vs committed goldens; adapter differs from golden host)
  PIX nesting: Render / GBuffer / DeferredLighting / LightingDebug (or GBufferDebug)
Artifacts:
  src/renderer/HDRSceneColorTarget.h
  src/renderer/HDRSceneColorTarget.cpp
  src/renderer/DeferredLightingPass.h
  src/renderer/DeferredLightingPass.cpp
  src/renderer/LightingDebugPass.h
  src/renderer/LightingDebugPass.cpp
  src/shaders/deferred_lighting_vs.hlsl
  src/shaders/deferred_lighting_ps.hlsl
  src/shaders/lighting_debug_vs.hlsl
  src/shaders/lighting_debug_ps.hlsl
  tests/test_lighting_debug.cpp
  docs/lighting.md
  docs/capture-guide.md
  docs/g-buffer.md
  README.md
  IMPLEMENTATION_PLAN.md
  docs/PROGRESS.md
Known limitations:
  Diagnostic HDR is directional N·L only; ambient and point lights wait for S2.3
  No tone map; HDR is not presented raw
  No HDR golden / non-finite regression (S2.4)
  S1.6 --output / golden remains GBuffer-only
Next step: S2.3 - Implement directional and point-light shading
```

### S2.3 - Implement directional and point-light shading

```text
Step: S2.3
State: Complete
Date: 2026-09-05
Commit: 5688b7efd8b688ed9acb40132197406295e1ac6e
Commands:
  cmake --preset windows-vs2022 -DSHADERMAKE_FIND_DXC=OFF -DSHADERMAKE_DXC_PATH=<local dxc>
  cmake --build --preset windows-debug --parallel
  cmake --build --preset windows-release --parallel
  .\out\build\windows-vs2022\bin\Debug\RenderLabDataContractTests.exe
  .\out\build\windows-vs2022\bin\Release\RenderLabDataContractTests.exe
  .\out\build\windows-vs2022\bin\Debug\RenderLab.exe --help
  .\out\build\windows-vs2022\bin\Debug\RenderLab.exe --headless --frames 8
  .\out\build\windows-vs2022\bin\Debug\RenderLab.exe --verify-lights --lock-camera --frames 8
  .\out\build\windows-vs2022\bin\Debug\RenderLab.exe --scene fallback-boxes --lighting-view lit --lock-camera --frames 8
  .\out\build\windows-vs2022\bin\Debug\RenderLab.exe --lock-camera --dump-lighting-views captures\s23-lighting-views
  .\out\build\windows-vs2022\bin\Debug\RenderLab.exe --gbuffer-view base-color --lighting-view lit
  powershell -NoProfile -File scripts\golden.ps1 -Mode Verify -Configuration Debug
Automated tests: RenderLabDataContractTests Debug and Release (0 failures)
  existing S1.2 / S1.3 / S1.4 / S1.5 / S1.6 / S2.1 checks
  lighting debug CLI parse (world-position, ndotl, lit, aliases, rejection)
  PresentSource defaults to LightingDebug + lit
  --verify-lights fixture constants
  DeferredLighting / LightingDebug raster: cull none, depth test/write off
GPU validation/capture:
  Debug: NVIDIA GeForce RTX 5060 Laptop GPU, driver 32.0.15.7322, NVRHI D3D12,
    validation=NVRHI + D3D12 debug runtime, DXR 1.1, SM 6.7, errors=0
  HDRSceneColor created 1280x720 RGBA16_FLOAT approxBytes=7372800
  Default present is lighting lit (no view flags)
  --verify-lights headless errors=0
  --dump-lighting-views wrote lighting-world-position.png, lighting-ndotl.png,
    lighting-lit.png
  --gbuffer-view and --lighting-view together exit 2
  S1.6 golden.ps1 Verify still passes (mae=0 vs committed goldens; adapter differs)
  PIX nesting: Render / GBuffer / DeferredLighting / LightingDebug (or GBufferDebug)
Artifacts:
  src/shaders/lighting.hlsli (BRDF helpers)
  src/shaders/deferred_lighting_ps.hlsl
  src/shaders/lighting_debug_ps.hlsl
  src/shaders/lighting_debug_cb.h
  src/renderer/LightingContract.h
  src/renderer/LightingDebugPass.h/.cpp
  src/renderer/DeferredLightingPass.cpp/.h
  src/app/RenderingLabApp.cpp/.h
  src/app/main.cpp
  tests/test_lighting_debug.cpp
  tests/test_lighting_contract.cpp
  scenes/manifest.json (fallback-boxes SHA-256 refreshed to match on-disk glTF)
  docs/lighting.md
  docs/capture-guide.md
  docs/g-buffer.md
  docs/PROGRESS.md
  README.md
  IMPLEMENTATION_PLAN.md
Known limitations:
  No tone map; lit debug is Reinhard visualization only
  No HDR golden / non-finite regression (S2.4)
  Default scene metallic is ~0; use fallback-boxes for metal checklist
  S1.6 --output / golden remains GBuffer-only
Next step: S2.4 - Add lighting validation and regression output
```

### S2.4 - Add lighting validation and regression output

```text
Step: S2.4
State: Complete
Date: 2026-09-06
Commit: f442f8d3afe4610abcb7057264044332bc192872
Commands:
  cmake --preset windows-vs2022 -DSHADERMAKE_FIND_DXC=OFF -DSHADERMAKE_DXC_PATH=<local dxc>
  cmake --build --preset windows-debug --parallel
  cmake --build --preset windows-release --parallel
  .\out\build\windows-vs2022\bin\Debug\RenderLabDataContractTests.exe
  .\out\build\windows-vs2022\bin\Debug\RenderLab.exe --headless --lock-camera --output-hdr results\s24-run
  powershell -NoProfile -File scripts\golden-hdr.ps1 -Mode Verify -Configuration Debug
  powershell -NoProfile -File scripts\golden.ps1 -Mode Verify -Configuration Debug
Automated tests: RenderLabDataContractTests Debug (0 failures)
  existing S1.2 / S1.3 / S1.4 / S1.5 / S1.6 / S2.1 / S2.3 checks
  .rlhdr header roundtrip, finite/non-finite scan, mixed abs/rel compare, metadata identity
  HDR self-compare (two Debug captures vs committed goldens, mae=0)
  S1.6 golden.ps1 Verify still passes
GPU validation/capture:
  Debug: NVIDIA GeForce RTX 5060 Laptop GPU, driver 32.0.15.7322, NVRHI D3D12
  HDRSceneColor 1280x720 RGBA16_FLOAT; .rlhdr size=7372832 (32-byte header + 7372800 payload)
  --output-hdr wrote three files: hdr-scene-color.rlhdr, lighting-lit.png, hdr-capture-metadata.json
  nonFiniteCount=0; two captures mae=0 / mismatchFraction=0 vs committed goldens
  PIX nesting: Render / GBuffer / DeferredLighting / LightingDebug (or GBufferDebug)
  PIX was not relaunched for S2.4; nesting is unchanged from S2.3 (code + Task 9 GPU capture
    on the same adapter/driver). DumpHdrCapture is a CPU staging readback of HDRSceneColor
    after present; it does not add a GPU lighting pass or extra UAV.
  HDRSceneColor remains RT+SRV, isUAV=false; DeferredLighting reads declared GBuffer SRVs
Artifacts:
  src/renderer/HdrDump.h
  tests/hdr_compare.h
  tests/hdr_compare.cpp
  tests/test_hdr_compare.cpp
  --output-hdr (src/app/main.cpp, RenderingLabApp::DumpHdrCapture)
  scripts/golden-hdr.ps1
  tests/golden-hdr/cesium-milk-truck/s04-default/1280x720/
  docs/hdr-regression.md
Known limitations:
  No tone map; lit debug is Reinhard visualization only
  Milk Truck metallic is a weak oracle; use fallback-boxes for the metal checklist
  S1.6 --output / golden.ps1 remains GBuffer-only
  --verify-lights / --scene fallback-boxes is not an HDR golden
Next step: S3.1 - Freeze exposure and output-transfer policy
```

### S3.1 - Freeze exposure and output-transfer policy

```text
Step: S3.1
State: Complete
Date: 2026-09-07
Commit: cf471109a48d5548fe7a37d34589e91e38f0b872
Follow-up review: 350d0a9d844670a7898d473d26aa3674ba914341 (HLSL select() for vector
  conditions; literal cross-check script; dxc smoke compile; entry-clamp and
  FromAP1 documentation)
Commands:
  cmake --build --preset windows-debug --parallel
  cmake --build --preset windows-release --parallel
  .\out\build\windows-vs2022\bin\Debug\RenderLabDataContractTests.exe
  .\out\build\windows-vs2022\bin\Release\RenderLabDataContractTests.exe
  powershell -NoProfile -File scripts\golden.ps1 -Mode Verify -Configuration Debug
  powershell -NoProfile -File scripts\golden-hdr.ps1 -Mode Verify -Configuration Debug
  powershell -NoProfile -File scripts\smoke.ps1
  python scripts\postprocess_reference.py
  python scripts\postprocess_crosscheck.py
  dxc -T ps_6_0 -E PostprocessHlslSmokePS -I src\shaders out\tmp-s31\postprocess_hlsl_smoke.hlsl
    (temporary pixel-shader entry that includes postprocess.hlsli and calls the chain)
Automated tests: RenderLabDataContractTests Debug and Release (0 failures)
  existing S1.2 / S1.3 / S1.4 / S1.5 / S1.6 / S2.1 / S2.3 / S2.4 checks
  TonemapConstants layout: 16 bytes, exposureEV @ 0, pads @ 4/8/12; default EV 0
  EV scale: 0 -> 1, +1 -> 2, -1 -> 0.5, +3 -> 8, -2.5 -> 2^-2.5
  frozen UE 5.8.1 constants: film curve 0.88/0.55/0.26/0/0.04, blue correction 0.6,
    expand gamut 1.0, RRT sweeteners, desaturation 0.96/0.93, YC weight 1.75
  base-matrix literal spot checks; composite matrices match the float64 reference:
    working<->AP1 incl. Bradford D65->D60 CAT, blue-correct conjugates, expand matrix
  full-chain battery: 19 vectors vs scripts/postprocess_reference.py (tol 1e-5;
    fp32-vs-float64 observed delta < 6e-8)
  black -> exactly 0; FilmToneMap keeps AP1 0.18 neutral at 0.18; whole-chain mid-gray
    fixed point; gray ramp 0.001..100 monotonic; EV+1 equals doubling the input
  postprocess_crosscheck.py: 11 matrices + AP1_RGB2Y + 18 scalar literals identical
    across UE 5.8.1 sources, PostProcessContract.h, and postprocess.hlsli
    (UE matrices compared transposed); review pass 2026-09-07
  dxc ps_6_0 smoke compile of postprocess.hlsli via a temporary entry (review pass)
GPU validation/capture: not applicable; S3.1 is a contract freeze (S2.1 precedent).
  Post-change regression on the current adapter: golden.ps1 and golden-hdr.ps1 Verify
  pass (two captures each, mae=0, channel swap fails as expected); smoke.ps1 Debug and
  Release errors=0 (NVIDIA GeForce RTX 4070 SUPER, validation=NVRHI + D3D12 debug
  runtime in Debug).
Artifacts:
  docs/postprocess.md
  src/shaders/postprocess_cb.h
  src/shaders/postprocess.hlsli
  src/renderer/PostProcessContract.h
  tests/test_postprocess_contract.cpp
  scripts/postprocess_reference.py
  src/CMakeLists.txt
  tests/CMakeLists.txt
  tests/test_renderer_data.cpp
  docs/renderer-conventions.md
  docs/lighting.md
  README.md
  IMPLEMENTATION_PLAN.md
  docs/PROGRESS.md
Known limitations:
  postprocess.hlsli is not yet included by a repo shader; it is dxc smoke-compiled
    (ps_6_0, temporary entry) and literal-checked against UE until S3.2 wires it
    into PostProcessPass
  --exposure-ev CLI parsing lands in S3.2; kExposureEvCli is reserved in the contract
  No GPU tone-map pass, no LDR golden, no UI or present-path change in this step
  The curve is verified on CPU against the float64 reference; GPU evaluation is S3.2
Next step: S3.2 - Implement tone mapping and present
```

### S3.2 - Implement tone mapping and present

```text
Step: S3.2
State: Complete
Date: 2026-09-07
Commit: (pending)
Commands:
  cmake --build --preset windows-debug --parallel
  cmake --build --preset windows-release --parallel
  .\out\build\windows-vs2022\bin\Debug\RenderLabDataContractTests.exe
  .\out\build\windows-vs2022\bin\Release\RenderLabDataContractTests.exe
  powershell -NoProfile -File scripts\golden.ps1 -Mode Verify -Configuration Debug
  powershell -NoProfile -File scripts\golden-hdr.ps1 -Mode Verify -Configuration Debug
  powershell -NoProfile -File scripts\smoke.ps1
  .\out\build\windows-vs2022\bin\Debug\RenderLab.exe --headless --lock-camera --output-hdr results\s32-baseline-a
  .\out\build\windows-vs2022\bin\Debug\RenderLab.exe --headless --lock-camera --output-hdr results\s32-baseline-b
  .\out\build\windows-vs2022\bin\Debug\RenderLabGoldenCompare.exe --mode hdr --candidate results\s32-baseline-a --reference results\s32-baseline-b
  .\out\build\windows-vs2022\bin\Debug\RenderLab.exe --headless --lock-camera --exposure-ev <ev> --output-hdr results\s32-ev-<ev>  (ev in -3, -1, 1, 3)
  python scripts\ldr_stats.py results\s32-ev--3 results\s32-ev--1 results\s32-baseline-a results\s32-ev-1 results\s32-ev-3
  .\out\build\windows-vs2022\bin\Debug\RenderLab.exe --headless --frames 10000
  .\out\build\windows-vs2022\bin\Debug\RenderLab.exe --frames 200000  (windowed ~5.5 min; auto resize + minimize/restore probe)
  pixtool launch ... --command-line="--lock-camera --frames 16" take-capture --frames=1 save-capture captures\s32-final.wpix
  pixtool open-capture captures\s32-final.wpix save-event-list captures\s32-final-events.csv
  pixtool open-capture captures\s32-final.wpix save-screenshot captures\s32-final-screenshot.png
Automated tests: RenderLabDataContractTests Debug and Release (0 failures)
  existing S1.2 / S1.3 / S1.4 / S1.5 / S1.6 / S2.1 / S2.3 / S2.4 / S3.1 checks
  post-process raster/depth contract: cull none, depth test/write off
  ParseExposureEv accepts 0 / -3 / +2.5 / 1e1 / -0.0 and unclamped +/-15; rejects
    empty / abc / 2.5x / nan / inf / -inf / 1e400 without modifying the output value
  PresentSource::Final is 2; kDefaultPresentSource == Final (app and test share the
    renderer-layer constant; the old vacuous local-variable default check is now real)
  HDR metadata v2: exposureEV and finalFileName required; EV 1 loads but is rejected
    against the locked EV 0; EV as a JSON string / 1e400 / missing field are errors;
    wrong finalFileName rejected against the locked capture
  final.png directory compare: missing final.png is an error; 2x2 final.png is not the
    locked resolution; R/B-swapped final.png is a regression and fails the tight 8-bit
    rule; matching fixture final.png passes
  committed goldens self-compare passes including the final.png tight rule
GPU validation/capture:
  Debug: NVIDIA GeForce RTX 4070 SUPER, driver 32.0.15.7688, NVRHI D3D12,
    validation=NVRHI + D3D12 debug runtime, errors=0 (every run below)
  Default present (no view flags) is the tone-mapped final; --gbuffer-view /
    --lighting-view unchanged and still mutually exclusive (exit 2); ImGui radio +
    hotkey 0 select Final
  --exposure-ev edge cases: abc / nan / inf / 1e400 / missing value exit 2;
    --exposure-ev 1 --gbuffer-view base-color exits 0 (inert for debug views)
  EV sweep -3/-1/0/+1/+3 (scripts\ldr_stats.py; linear Rec.709 luma decoded from
    final.png): mean Y 0.005778 / 0.035744 / 0.061226 / 0.085432 / 0.127462
    (monotonic; median 0 is the black scene background)
    single-stop mid/low-luma ratios (mask Y < 0.5): -1 -> 0 = 1.829, 0 -> +1 = 1.902
    (inside the 1.6-2.4 observation band); two-stop: -3 -> -1 = 6.19 (toe lifts above
    the naive 4x), +1 -> +3 = 3.15 (shoulder compresses below the naive 4x)
  highlight rolloff at EV +3: a naive exposure+hard-clip would flatten 11.81% of
    pixels (any scene channel x 2^3 > 1.0, computed from the committed .rlhdr); the
    tone-mapped final.png saturates only 1.67% pure white while 10.78% of pixels
    stay bright (Y >= 0.5) but graded
  golden-hdr re-baseline (S1.6 precedent; schema v2 + final.png force it): two Debug
    captures bit-identical (mae 0 / mismatch 0 for both .rlhdr and final.png); the
    new .rlhdr is also byte-identical to the S2.4 baseline minted on the
    RTX 5060 Laptop GPU, so the deferred path is deterministic across both adapters;
    approved adapter is now the RTX 4070 SUPER; golden-hdr.ps1 Verify passes
    (two fresh captures + R/B swap fails as expected)
  PIX (captures\s32-final.wpix event list): Frame > Render > { SceneUpdate, GBuffer,
    DeferredLighting, PostProcess }, then UI (ImGUI), then Present
  UI composes over the tone-mapped output: the PIX presented-frame screenshot
    differs from final.png only inside the ImGui panel bounding box
    (rows 16..667, cols 16..495); outside it the max-channel diff is 0 for every pixel
  resize + minimize + long runs: windowed --frames runs resize 1280x720 -> 1344x784
    then minimize + restore; a 200000-frame windowed run (~5.5 min) and a headless
    10000-frame run both finished errors=0
Artifacts:
  src/shaders/postprocess_vs.hlsl
  src/shaders/postprocess_ps.hlsl
  src/shaders/Shaders.cfg
  src/renderer/PostProcessPass.h
  src/renderer/PostProcessPass.cpp
  src/renderer/LightingDebugPass.h  (PresentSource::Final + kDefaultPresentSource)
  src/app/FrameMarkers.h
  src/app/FrameMarkers.cpp  (kPostProcess)
  src/app/RenderingLabApp.h
  src/app/RenderingLabApp.cpp
  src/app/main.cpp
  src/CMakeLists.txt
  tests/hdr_compare.h
  tests/hdr_compare.cpp
  tests/golden_compare_main.cpp
  tests/test_postprocess_pass.cpp
  tests/test_lighting_debug.cpp
  tests/test_hdr_compare.cpp
  tests/test_renderer_data.cpp
  tests/CMakeLists.txt
  scripts/golden-hdr.ps1
  scripts/ldr_stats.py
  tests/golden-hdr/cesium-milk-truck/s04-default/1280x720/  (re-baselined: final.png
    added, metadata v2, manifest final rules; .rlhdr and lighting-lit.png unchanged bytes)
  docs/postprocess.md
  docs/hdr-regression.md
  docs/capture-guide.md
  README.md
  IMPLEMENTATION_PLAN.md
  docs/PROGRESS.md
Known limitations:
  The LDR golden pins EV 0; a capture at any other EV fails the identity check by
    design (clean "Exposure mismatch" regression before pixel compare)
  final.png covers the tone-mapped output only; the ImGui overlay itself has no golden
  The final.png portability band reuses the S1.6 default tolerances from same-adapter
    mae=0 captures and was not exercised on a second GPU
  ldr_stats.py is evidence tooling, not a gate; the 1.6-2.4 single-stop band is an
    observation window, not a contract
  The PostProcess GPU timestamp feeds the ImGui HUD only; per-pass timing export is S3.3
Next step: S3.3 - Freeze the manual-pipeline reference
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
