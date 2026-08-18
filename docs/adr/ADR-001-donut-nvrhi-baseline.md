# ADR-001: Donut / NVRHI Baseline

- Status: Accepted
- Date: 2026-08-18
- Step: S0.1
- Commit recorded in: `dependencies.lock.json`

## Decision

RenderLab will consume NVIDIA Donut commit
`bfdebdd7dd5455c503b2737a1967a4ef651c145b` as a recursive Git submodule. That
revision pins NVRHI to `8e8c36e37558acec333204619b95d9d2fcdc4a79`. The sole
source bootstrap path is recursive Git submodules. vcpkg is not introduced.

The first RenderLab application will subclass `donut::app::ApplicationBase` and
let `donut::app::DeviceManager` own the window, D3D12 device, queues, fences,
and swap chain. The smallest upstream sample that demonstrates that ownership
is Donut-Samples `examples/basic_triangle`. FeatureDemo is a catalog of later
features, not the implementation template.

## Context

`IMPLEMENTATION_PLAN.md` requires one exact Donut commit, the transitive NVRHI
commit, a recorded acquisition method, and a named application-class reference
before any submodule is added. Donut has no release tags. The official tree is
now `NVIDIA-RTX/Donut`; the older `NVIDIAGameWorks/donut` name is obsolete.

RenderLab must not recreate a Device, Queue, Fence, SwapChain, or Win32
runtime. Those types remain on `backup/legacy-d3d12-20260818` at `856b4c2`.

## Options Considered

### 1. Track `main` or a floating tag

Rejected. The plan forbids branch or tag tracking. Donut has no tags anyway.

### 2. Pin the Donut commit currently used by Donut-Samples

Donut-Samples `d766a7697b67` (2026-06-11) still points at Donut
`69082638b014e3dae85a42c44b63e4187857c2c9`. That revision predates the NVRHI
enhanced-barrier update and the Windows ARM64 / Agility SDK path fix. It is a
useful sample snapshot, not the best framework baseline.

### 3. Pin an older NVRHI that avoids DirectX-Headers preview packages

Rejected. The D3D12 backend in the current official NVRHI requires
DirectX-Headers. The preview tag is real, so this ADR locks the resolved
commit `d873b344dc540898868697245f100c2a67fe68d9` instead of the tag name.

### 4. Pin Donut `bfdebdd7dd5455c503b2737a1967a4ef651c145b`

Selected. This is the newest merge on `main` as of 2026-08-18. It is a
completed merge, not a work-in-progress branch. Recursive checkout succeeded
on Windows. The revision includes D3D12, DXR, ray-tracing validation
plumbing, and ShaderMake/DXC 6.6-capable compilation.

## Why This Donut Revision

The selected commit is dated 2026-07-31 and merges
`ttcheblokov/windows-arm64-build-fix`. Relative to the June Donut-Samples pin
it also contains:

- NVRHI `8e8c36e37558acec333204619b95d9d2fcdc4a79`, which keeps first-class
  D3D12 ray tracing and adds optional enhanced barriers.
- `DeviceManager_DX12` plumbing for `enableRayTracingValidation`.
- ShaderMake `5daebdbef45088fc2369d441391ecab0eba25e54` with DXC `v1.9.2602`.
- An architecture-aware Agility SDK binary path. RenderLab will not fetch
  Agility SDK in S0.2 unless configure/build proves the installed Windows SDK
  is insufficient.

The ARM64 change is a build-path fix, not a renderer feature. Enhanced
barriers remain optional and will stay off for the first raster and DXR
slices, matching `NEW_PLAN.md`.

## Why This NVRHI Revision

NVRHI `8e8c36e37558acec333204619b95d9d2fcdc4a79` is the exact submodule commit
recorded by the selected Donut tree. It is not an independently chosen NVRHI
tip. Evidence from that tree:

- `src/d3d12/d3d12-raytracing.cpp` implements BLAS, TLAS, RTPSO, and dispatch.
- `IDevice` exposes `createRayTracingPipeline`, `createAccelStruct`,
  `buildBottomLevelAccelStruct`, `buildTopLevelAccelStruct`, and related
  command-list APIs.
- `queryFeatureSupport` maps `RayTracingAccelStruct` / `RayTracingPipeline`
  to DXR 1.0 and `RayQuery` to DXR 1.1.
- `DeviceManager_DX12` creates the D3D12 device and queues, then wraps them
  with `nvrhi::d3d12::createDevice`. RenderLab must not create those objects.

NVRHI has no nested Git submodules. DirectX-Headers is fetched by CMake.

## Acquisition Method

Sole source bootstrap path: recursive Git submodules.

S0.2 will add Donut at `external/donut` and initialize the six submodules
recorded in `.gitmodules`:

| Path | Commit |
|---|---|
| `nvrhi` | `8e8c36e37558acec333204619b95d9d2fcdc4a79` |
| `ShaderMake` | `5daebdbef45088fc2369d441391ecab0eba25e54` |
| `thirdparty/imgui` | `45acd5e0e82f4c954432533ae9985ff0e1aad6d5` |
| `thirdparty/glfw` | `7b6aead9fb88b3623e3b3725ebb42670cbe4c579` |
| `thirdparty/cgltf` | `fa3b80fa762790192c9532b63c441627416ff300` |
| `thirdparty/stb` | `2e2bef463a5b53ddf8bb788e25da6b8506314c08` |

JsonCpp 1.9.6 is already amalgamated inside Donut and is not a submodule.

CMake FetchContent is allowed only for:

1. DirectX-Headers commit `d873b344dc540898868697245f100c2a67fe68d9`.
2. DXC release `v1.9.2602` / `dxc_2026_02_20.zip`, SHA-256
   `A1E89031421CF3C1FCA6627766AB3020CA4F962AC7E2CAA7FAB2B33A8436151E`.

vcpkg is not used. Streamline, DLSS, Aftermath, RTXMU, NVAPI, Vulkan, DX11,
Slang, and Agility SDK are disabled or not fetched.

## Intended CMake Shape For S0.2

Enable only the D3D12 desktop path:

- `DONUT_WITH_NVRHI=ON`
- `DONUT_WITH_DX12=ON`
- `DONUT_WITH_DX11=OFF`
- `DONUT_WITH_VULKAN=OFF`
- `DONUT_WITH_STREAMLINE=OFF`
- `DONUT_WITH_DLSS=OFF`
- `DONUT_WITH_AFTERMATH=OFF`
- `DONUT_WITH_UNIT_TESTS=OFF`
- `NVRHI_WITH_VALIDATION=ON`
- `NVRHI_WITH_RTXMU=OFF`
- `NVRHI_DIRECTX_HEADERS_GIT_TAG=d873b344dc540898868697245f100c2a67fe68d9`
- `SHADERMAKE_FIND_DXC=ON`
- `SHADERMAKE_FIND_FXC=OFF`
- `SHADERMAKE_FIND_DXC_VK=OFF`
- `SHADERMAKE_FIND_SLANG=OFF`

Donut is not a standalone executable. RenderLab will include it from the
root CMake after S0.2 pins the submodule.

## Shader Path

Application and Donut shaders go through `donut_compile_shaders()` in
`compileshaders.cmake`. That helper calls ShaderMake, which calls the pinned
DXC and writes DXIL blobs. The helper default shader model is `6_5`.
RenderLab shaders that need Shader Model 6.6 must pass `SHADER_MODEL 6_6`
explicitly. This does not require a Donut source edit.

## Integration Reference

| Need | Upstream type or sample | Use |
|---|---|---|
| Window, device, swap chain, message loop | `donut::app::DeviceManager` | own the low-level runtime |
| Scene-capable application shell | `donut::app::ApplicationBase` | subclass in S0.3 / S0.4 |
| Smallest D3D12 present loop | Donut-Samples `examples/basic_triangle` | first application shape |
| Headless / fixed-frame later | Donut-Samples `examples/headless` | command-line and CI smoke |
| Later GBuffer comparison only | Donut-Samples `examples/deferred_shading` | inspect, do not adopt as RDG |
| Later DXR API comparison only | `examples/rt_triangle`, `examples/rt_shadows` | inspect NVRHI RT usage |
| Not a template | `feature_demo/FeatureDemo.cpp` | too many excluded features |

Donut already contains `GBufferFillPass`, `DeferredLightingPass`, and tone
mapping. Those passes are outside the learning goal. RenderLab will implement
its own passes and Mini RDG above NVRHI. Donut scene loading, TextureCache,
ShaderFactory, CommonRenderPasses, and ImGui remain reusable.

## Ownership Boundary

Donut / NVRHI owns:

- application lifecycle, window, swap chain, device, queues, fences, command lists
- physical buffers, textures, views, bindings, pipelines, and submission
- shader build integration and glTF loading
- low-level resource-state execution

RenderLab owns:

- renderer passes and their contracts
- Mini RDG
- DXR scene policy, AS scheduling, and SBT policy
- tests, captures, and benchmarks

Any Donut or NVRHI source edit requires a separate commit and a new ADR.

## Consequences

- S0.2 can add `external/donut` at the locked commit without researching
  versions again.
- DirectX-Headers must be pinned by commit in CMake. Leaving
  `v1.717.0-preview` in the cache would re-introduce a floating tag.
- CMake 3.25.3 is the validated local version. Donut's README asks for 3.31.
  S0.2 must treat a configure failure as an environment upgrade, not as
  permission to change Donut.
- Agility SDK, Streamline, and DLSS stay out until a later step has a
  concrete need.
- The retired from-scratch D3D12 runtime stays on
  `backup/legacy-d3d12-20260818`.

## Validation Performed In S0.1

- Inspected Donut `main` history through 2026-08-18. No tags exist.
- Checked out `bfdebdd7dd5455c503b2737a1967a4ef651c145b` on Windows.
- Initialized recursive submodules. `git submodule status --recursive` had
  no leading `-`. NVRHI has no nested Git submodules.
- Read Donut, NVRHI, and ShaderMake CMake, DeviceManager, ApplicationBase,
  and NVRHI ray-tracing headers.
- Resolved DirectX-Headers `v1.717.0-preview` to
  `d873b344dc540898868697245f100c2a67fe68d9`.
- Downloaded DXC `dxc_2026_02_20.zip` and hashed it as
  `A1E89031421CF3C1FCA6627766AB3020CA4F962AC7E2CAA7FAB2B33A8436151E`.
- Recorded the local VS, MSVC, Windows SDK, CMake, GPU driver, and PIX
  versions in `docs/build-environment.md`.
