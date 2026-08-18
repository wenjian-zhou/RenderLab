## 1. 修订后的项目定位

### 1.1 项目名称

**Donut NVRHI Rendering Lab**

### 1.2 核心目标

基于 **NVIDIA Donut + NVRHI** 搭建一个轻量实时渲染实验框架，重点实现两类接近 Unreal Engine 的核心架构：

1. **简化版 Render Dependency Graph（RDG）**
2. **简化版 Ray Tracing Scene / DXR 架构**

项目的基础渲染功能按以下顺序建设：

```text
Donut/NVRHI 基础运行
    ↓
光栅化 GBuffer
    ↓
Deferred Shading
    ↓
基础 Post Processing
    ↓
简化版 RDG
    ↓
基于 RDG 重构基础渲染管线
    ↓
DXR Scene / BLAS / TLAS 架构
    ↓
Ray Tracing Pass
```

项目不再以“DXR Benchmark 作为第一纵向切片”为主，而是先建立一个有清晰 Pass、资源和生命周期模型的渲染器骨架。DXR 架构建立在这个骨架之上。

### 1.3 目标平台

- Windows 11
- **D3D12 + DXR 1.1**
- HLSL Shader Model 6.6+
- C++20
- CMake
- Donut + NVRHI
- NVIDIA GPU 作为首要验证平台
- AMD RDNA GPU 作为后续可选验证平台

### 1.4 设计原则

\[
\text{Correctness} > \text{Observability} > \text{Maintainability} > \text{Generality}
\]

- 先保证渲染结果、资源访问和 GPU 生命周期正确；
- 每个 Pass 和资源都必须可观测、可调试、可统计；
- 只实现当前项目需要的最小抽象；
- 不复制 Unreal Engine 的全部复杂性；
- 不实现完整 RHI，不同时维护 D3D12/Vulkan 两条路径；
- 不以功能数量作为完成标准，以架构是否可解释、可验证为标准。

---

## 2. 与上一版计划的主要变化

### 2.1 原计划

原计划以 DXR Systems Benchmark 为中心，优先研究：

- BLAS Update/Rebuild；
- `TraceRay` 与 Inline `RayQuery`；
- Payload、Any-Hit、Alpha Test；
- 动态细长几何；
- GPU 性能对照。

### 2.2 修订后的计划

修订后先完成一个最小但结构完整的渲染框架：

- 基础应用与场景；
- GBuffer；
- Deferred Shading；
- Post Processing；
- 简化 RDG；
- 基于 RDG 的 Pass 编译和执行；
- DXR Scene 管理；
- BLAS/TLAS 生命周期；
- Ray Tracing Pipeline 与 Ray Tracing Pass。

性能实验会保留，但放到基础架构稳定之后进行。

### 2.3 为什么这样调整

你的目标不是只做一个孤立的光追实验，而是补齐：

- 渲染器结构设计；
- Pass/Resource 依赖；
- 资源生命周期；
- Barrier 和同步；
- 场景级 Ray Tracing 架构；
- 从基础光栅到 DXR 的完整演进路径。

这条路径更接近 Unreal Engine Renderer 的实际组织方式，也更适合形成一套完整的图形引擎作品。

---

## 3. 技术边界

### 3.1 Donut/NVRHI 负责的内容

继续复用 Donut 和 NVRHI 的基础能力：

- 应用启动、窗口和 SwapChain；
- D3D12 Device、Queue、Fence 和 Command List 封装；
- Buffer、Texture、Sampler 和资源视图；
- Binding Layout、Binding Set 和 Pipeline 创建；
- Shader 编译和加载；
- glTF 场景、Mesh、材质和相机；
- 基础 ImGui；
- NVRHI 的 D3D12 后端；
- 低层资源状态和命令提交能力。

### 3.2 自己实现的内容

#### 渲染器基础

- GBuffer Pass；
- Deferred Lighting Pass；
- Post Processing Pass；
- Pass 参数和资源声明；
- Frame 常量和场景数据组织；
- Debug View；
- GPU 时间统计。

#### 简化 RDG

- Logical Resource；
- Resource Version；
- Pass Read/Write 声明；
- RAW/WAR/WAW 依赖；
- Pass DAG；
- Topological Sort；
- Pass Culling；
- First/Last Use 分析；
- 基础 Resource State/Barrier Planning；
- 基础 Transient Resource；
- Graph Debug Dump。

#### 简化 DXR 架构

- Ray Tracing Scene；
- RT Geometry；
- RT Instance；
- BLAS Registry；
- TLAS Manager；
- Build/Update/Rebuild 策略；
- RT Pipeline；
- Shader Binding Table 管理；
- Ray Tracing Material/Hit Group 映射；
- Ray Tracing Pass；
- 延迟资源释放；
- AS 和 Ray Dispatch 统计。

### 3.3 第一阶段不做

- 完整 Unreal 风格 RHI；
- 完整跨平台 RDG；
- 完整材质系统和编辑器；
- ReSTIR、完整 RTGI 和复杂 Denoiser；
- 多队列调度作为基础功能；
- 完整 ECS、动画、物理和游戏逻辑；
- 复杂资产管线；
- 直接修改 Donut/NVRHI 公共抽象。

---

## 4. 总体架构

```text
Application
├── RenderingLabApp
├── Interactive Mode
└── Benchmark Mode

Renderer
├── Scene Renderer
├── GBuffer Renderer
├── Deferred Lighting
├── Post Processing
├── Ray Tracing Renderer
└── Debug Visualization

Mini RDG
├── Graph Builder
├── Logical Resources
├── Resource Versions
├── Pass Registry
├── Dependency Compiler
├── Pass Culling
├── Lifetime Analysis
├── Barrier Planner
└── Graph Debugger

DXR Architecture
├── RayTracingScene
├── RTGeometry
├── RTInstance
├── BLASRegistry
├── TLASManager
├── RTPSO / Shader Binding Table
├── RayTracingMaterialBinding
└── RayTracingPass

Donut / NVRHI
├── Application
├── Scene and Asset Loading
├── Resource and Binding Abstractions
├── Shader Management
├── D3D12 Backend
└── Command Submission

D3D12 + DXR 1.1
```

### 4.1 架构边界

项目不实现一套新的底层 RHI，而是在 NVRHI 之上实现渲染器、Mini RDG 和 DXR Scene 层：

```text
NVRHI Resource / Command / Binding
                ↑
       Mini RDG + Renderer
                ↑
        DXR Scene Architecture
```

这样可以将精力集中在：

- UE 风格的资源依赖模型；
- 渲染 Pass 编排；
- Ray Tracing 场景生命周期；
- 算法和系统之间的连接。

---

## 5. 基础光栅渲染管线

基础渲染功能必须先于 DXR 完成。

### 5.1 GBuffer 设计

第一版建议使用 Deferred Shading 所需的最小 GBuffer：

| 资源 | 格式建议 | 内容 |
|---|---|---|
| `GBuffer0` | `RGBA8_UNORM` 或 `R8G8B8A8_UNORM_SRGB` | Base Color、可选 Material 标记 |
| `GBuffer1` | `RGBA16_FLOAT` | World Normal、可选 Roughness |
| `GBuffer2` | `RGBA8_UNORM` | Metallic、Roughness、AO、材质标记 |
| `Depth` | `D32_FLOAT` | 深度 |
| `Velocity` | `RG16_FLOAT`，可选 | 后续时域或运动调试 |

实际格式以硬件支持和画质需求为准。第一版不需要追求复杂压缩，但必须在文档中说明：

- 法线编码方式；
- 深度重建方式；
- Roughness/Metallic 的存储位置；
- sRGB 与 Linear 的边界；
- 是否采用 Reversed-Z；
- 纹理和 UAV/SRV 的访问状态。

### 5.2 GBuffer Pass

输入：

- Scene Mesh；
- Camera；
- Material Buffer；
- Transform Buffer。

输出：

- GBuffer0；
- GBuffer1；
- GBuffer2；
- Depth；
- 可选 Velocity。

逻辑：

```text
Scene
  ↓
Vertex Shader
  ↓
Rasterization
  ↓
Depth Test
  ↓
Material Evaluation
  ↓
GBuffer MRT
```

验收内容：

- Base Color Debug View；
- World Normal Debug View；
- Roughness/Metallic Debug View；
- Depth Debug View；
- 多材质场景；
- 相机移动和实例变换；
- 无 D3D12/NVRHI Validation 错误。

### 5.3 Deferred Shading Pass

输入：

- GBuffer0/1/2；
- Depth；
- Light Buffer；
- Camera；
- Shadow 或简单光照数据。

输出：

- HDR Scene Color。

第一版可以只实现：

- Directional Light；
- Point Light；
- 简单 Lambert + GGX；
- 基础阴影或无阴影版本；
- HDR 输出。

Deferred Lighting 的核心计算可以抽象为：

\[
L_o = L_e + \sum_k f_r(x,\omega_i,\omega_o) L_k V_k |n\cdot\omega_i|
\]

第一版不需要完整 Clustered Lighting。重点是让 GBuffer、资源读写和 Pass 组织稳定。

### 5.4 基础 Post Processing

第一版只实现最小、可验证的 Post Processing 链：

```text
HDR Scene Color
    ↓
Exposure
    ↓
Tone Mapping
    ↓
Gamma / sRGB Encode
    ↓
Back Buffer
```

可以选择加入：

- FXAA；
- 简单 Bloom；
- Color Grading；
- Debug Overlay。

不建议第一阶段加入完整 TAA，因为它会引入：

- Motion Vector；
- History Resource；
- Jitter；
- Disocclusion；
- 动态分辨率；
- 时域稳定性问题。

Post Processing 的主要作用是验证：

- 多 Pass 资源依赖；
- HDR/SDR 边界；
- UAV/SRV/RTV 状态切换；
- 输出资源生命周期；
- 基于 RDG 的 Pass 编排。

---

## 6. Mini RDG 设计

### 6.1 目标

实现一个小型、可解释的 UE 风格 RDG，而不是复制 Unreal Engine RDG 的全部功能。

第一版必须支持：

- Pass 声明资源访问；
- Logical Resource 和 Physical Resource 分离；
- Resource Version；
- Pass 依赖 DAG；
- Pass Culling；
- 拓扑排序；
- First/Last Use；
- 基础状态转换和 Barrier Plan；
- Graph 可视化。

### 6.2 核心概念

#### Logical Resource

在 Graph 构建阶段只描述资源，不立即创建底层 NVRHI Texture/Buffer：

```cpp
RGTextureDesc hdrDesc;
hdrDesc.name = "HDRSceneColor";
hdrDesc.width = viewport.width;
hdrDesc.height = viewport.height;
hdrDesc.format = Format::RGBA16_FLOAT;
hdrDesc.flags = TextureFlags::RenderTarget | TextureFlags::ShaderResource;

RGTexture hdr = graph.CreateTexture(hdrDesc);
```

#### Resource Version

每次写入产生新版本：

```text
Depth_v0
    ↓ GBuffer Write
Depth_v1
    ↓ Deferred Read

HDR_v0
    ↓ Deferred Write
HDR_v1
    ↓ Tone Mapping Read
```

#### Pass Declaration

```cpp
RGTexture depth;
RGTexture hdr;

graph.AddPass(
    "GBuffer",
    [&](RGPassBuilder& builder)
    {
        depth = builder.Write(depth);
        builder.Write(gbuffer0);
        builder.Write(gbuffer1);
        builder.Write(gbuffer2);
    },
    [&](RGContext& context)
    {
        RenderGBuffer(context);
    });

 graph.AddPass(
    "DeferredLighting",
    [&](RGPassBuilder& builder)
    {
        builder.Read(gbuffer0);
        builder.Read(gbuffer1);
        builder.Read(gbuffer2);
        builder.Read(depth);
        hdr = builder.Write(hdr);
    },
    [&](RGContext& context)
    {
        RenderDeferredLighting(context);
    });
```

### 6.3 Graph 编译流程

```text
Pass/Resource 声明
        ↓
Resource Versioning
        ↓
建立 RAW/WAR/WAW 依赖
        ↓
输出反向标记有效 Pass
        ↓
Pass Culling
        ↓
拓扑排序
        ↓
分析 First/Last Use
        ↓
创建 Physical Resource
        ↓
生成 Barrier Plan
        ↓
按顺序 Execute Pass
```

### 6.4 第一版访问类型

```cpp
enum class RGAccess
{
    Unknown,
    RenderTarget,
    DepthWrite,
    DepthRead,
    ShaderResource,
    UnorderedAccess,
    CopySource,
    CopyDest,
    RayTracingRead,
    AccelerationStructureBuild
};
```

访问类型映射到 NVRHI 的 ResourceState。第一版不要追求完整 D3D12 Enhanced Barriers；先使用 NVRHI 提供的状态表达和 Barrier 能力。

### 6.5 Pass Culling

Graph 必须能够从最终输出反向标记有效 Pass：

```text
GBuffer
  ↓
Deferred Lighting
  ↓
Tone Mapping
  ↓
Back Buffer Output
```

如果某个 Debug Pass 没有连接到输出，则可以被裁剪。调试模式提供：

- `--disable-pass-culling`；
- 强制保留某个 Pass；
- 输出被裁剪 Pass 列表。

### 6.6 Transient Resource

第一版只实现资源生命周期和基础复用，不做复杂 Heap Allocator：

```text
GBuffer0: [GBuffer, DeferredLighting]
GBuffer1: [GBuffer, DeferredLighting]
HDR:      [DeferredLighting, ToneMapping]
```

完成基础生命周期分析后，再考虑让生命周期不重叠的资源复用同一物理资源。

### 6.7 RDG 验收标准

- GBuffer、Deferred Lighting、Tone Mapping 能全部由 Graph 调度；
- Graph 能导出 Pass DAG；
- 能检测未声明的 Resource Read/Write；
- 能检测循环依赖；
- 无效 Pass 可裁剪；
- 资源版本和生命周期可打印；
- Debug Layer/NVRHI Validation 无错误；
- 手动状态转换数量明显少于未使用 Graph 的版本。

---

## 7. 类 UE 的 DXR 架构

### 7.1 目标

在 Mini RDG 之上实现一个简化的 Ray Tracing Scene 架构，重点学习 Unreal Engine 中常见的职责分离：

```text
Scene Geometry
    ↓
RayTracingScene
    ├── RTGeometry
    ├── BLAS Registry
    ├── RTInstance
    └── TLAS

RayTracingScene
    ↓
RayTracing Pass
    ├── RTPSO
    ├── Shader Binding Table
    ├── Global Bindings
    └── DispatchRays / RayQuery
```

### 7.2 RTGeometry

表示可以被 Ray Tracing 使用的一份几何数据：

```cpp
struct RTGeometry
{
    GeometryHandle geometry;
    BufferHandle vertexBuffer;
    BufferHandle indexBuffer;
    uint32_t vertexCount;
    uint32_t indexCount;
    bool allowUpdate;
    bool allowCompaction;
    bool isOpaque;
    GeometryBuildMode buildMode;
};
```

职责：

- 描述顶点/索引 Buffer；
- 描述几何格式；
- 记录静态/动态属性；
- 提供 BLAS Build 输入；
- 跟踪几何变更。

### 7.3 RTInstance

表示 TLAS 中的一份实例：

```cpp
struct RTInstance
{
    uint32_t instanceId;
    uint32_t instanceMask;
    uint32_t hitGroupOffset;
    Matrix3x4 transform;
    BLASHandle blas;
    MaterialHandle material;
    bool transformDirty;
};
```

职责：

- 绑定 BLAS；
- 维护 Transform；
- 维护 Instance Mask；
- 映射材质和 Hit Group；
- 生成 TLAS Instance Descriptor。

### 7.4 BLAS Registry

负责所有 BLAS 的生命周期和策略：

```text
Uninitialized
    ↓
PendingBuild
    ↓
Built
    ↓
PendingCompaction
    ↓
Compacted
    ↓
PendingUpdate / PendingRebuild
    ↓
Built
    ↓
Retired
```

第一版支持：

- Static BLAS；
- Dynamic BLAS；
- Update/Rebuild；
- 可选 Compaction；
- Scratch Buffer 统计；
- GPU Fence 延迟回收。

策略模型：

\[
T_{total}=T_{AS}+T_{trace}+T_{shade}+T_{sync}
\]

不要只依据 Build 时间选择 Update。需要观察 AS 质量变化对后续 Trace 的影响。

### 7.5 TLAS Manager

职责：

- 注册/删除实例；
- 更新 Transform；
- 处理 Instance Mask；
- 生成 TLAS Instance Buffer；
- Build/Update TLAS；
- 管理 TLAS 的跨帧生命周期；
- 向 Ray Tracing Pass 提供当前 TLAS。

第一版不做复杂多 TLAS 体系，只维护一个主 TLAS：

```text
Scene Update
    ↓
RTInstance Dirty List
    ↓
Update TLAS Instance Buffer
    ↓
Build or Update TLAS
    ↓
RayTracingScene Ready
```

### 7.6 RayTracingPipeline

负责：

- Ray Generation Shader；
- Miss Shader；
- Closest-Hit Shader；
- Hit Group；
- Ray Type；
- Global Binding；
- Shader Binding Table；
- Pipeline Cache。

第一版只支持：

- Shadow/AO Ray；
- 一个简单 Closest-Hit；
- 一个 Miss；
- 最小 Payload；
- 一个或两个 Ray Type。

### 7.7 RayTracingPass 与 RDG 的连接

DXR Pass 必须像普通渲染 Pass 一样声明依赖：

```cpp
graph.AddPass(
    "RayTracedShadow",
    [&](RGPassBuilder& builder)
    {
        builder.Read(depth);
        builder.Read(normal);
        builder.Read(rayTracingScene.tlas);
        shadow = builder.Write(shadowTexture);
    },
    [&](RGContext& context)
    {
        rayTracingPass.Execute(context);
    });
```

RDG 负责：

- 确认 TLAS Build 在 Ray Pass 前完成；
- 确认 Depth/Normal 的读取状态正确；
- 确认输出 UAV/Texture 状态正确；
- 生成 Pass 顺序和必要同步。

DXR 架构负责：

- 准备 RT Scene；
- 准备 BLAS/TLAS；
- 创建 RTPSO；
- 准备 SBT；
- Dispatch Ray；
- 输出 RT 统计。

### 7.8 第一版 DXR Pass 顺序

```text
GBuffer
    ↓
Build/Update BLAS
    ↓
Build/Update TLAS
    ↓
Ray Traced Shadow/AO
    ↓
Deferred Lighting Composite
    ↓
Tone Mapping
    ↓
Present
```

第一版不直接实现完整 RTGI。先使用 Shadow、AO 或 Visibility Ray 作为 DXR 架构负载，确保 Scene、AS、Pass 和资源依赖正确。

---

## 8. 推荐仓库结构

```text
RenderingLab/
├── CMakeLists.txt
├── CMakePresets.json
├── README.md
├── LICENSE
├── THIRD_PARTY_NOTICES.md
│
├── external/
│   └── donut/                         # 固定 commit 的 Submodule
│
├── src/
│   ├── app/
│   │   ├── RenderingLabApp.cpp
│   │   ├── InteractiveApp.cpp
│   │   └── CommandLine.cpp
│   │
│   ├── core/
│   │   ├── Config.cpp
│   │   ├── Logging.cpp
│   │   └── FileSystem.cpp
│   │
│   ├── renderer/
│   │   ├── SceneRenderer.cpp
│   │   ├── GBufferPass.cpp
│   │   ├── DeferredLightingPass.cpp
│   │   ├── PostProcessPass.cpp
│   │   └── DebugViewPass.cpp
│   │
│   ├── rdg/
│   │   ├── RDGBuilder.cpp
│   │   ├── RDGPass.cpp
│   │   ├── RDGResource.cpp
│   │   ├── RDGCompiler.cpp
│   │   ├── RDGBarrierPlanner.cpp
│   │   ├── RDGLifetime.cpp
│   │   └── RDGDebugger.cpp
│   │
│   ├── raytracing/
│   │   ├── RayTracingScene.cpp
│   │   ├── RTGeometry.cpp
│   │   ├── RTInstance.cpp
│   │   ├── BLASRegistry.cpp
│   │   ├── TLASManager.cpp
│   │   ├── RayTracingPipeline.cpp
│   │   ├── ShaderBindingTable.cpp
│   │   └── RayTracingPass.cpp
│   │
│   ├── benchmark/
│   │   ├── GPUProfiler.cpp
│   │   ├── BenchmarkRunner.cpp
│   │   ├── Statistics.cpp
│   │   └── ResultExporter.cpp
│   │
│   └── tests/
│       ├── RDGGraphTests.cpp
│       ├── RDGResourceLifetimeTests.cpp
│       ├── SBTLayoutTests.cpp
│       └── RTInstanceTests.cpp
│
├── shaders/
│   ├── common.hlsli
│   ├── gbuffer.hlsl
│   ├── deferred_lighting.hlsl
│   ├── tone_mapping.hlsl
│   ├── fxaa.hlsl
│   ├── raygen_shadow.hlsl
│   ├── miss_shadow.hlsl
│   ├── closesthit_shadow.hlsl
│   └── rayquery_ao.hlsl
│
├── scenes/
│   ├── manifest.json
│   └── README.md
│
├── scripts/
│   ├── bootstrap.ps1
│   ├── run_benchmark.ps1
│   ├── dump_rdg_graph.ps1
│   └── generate_report.py
│
└── docs/
    ├── design.md
    ├── build-environment.md
    ├── rdg.md
    ├── dxr-architecture.md
    ├── benchmark-methodology.md
    └── adr/
        ├── ADR-001-donut-nvrhi-base.md
        ├── ADR-002-mini-rdg-boundary.md
        ├── ADR-003-gbuffer-layout.md
        ├── ADR-004-rdg-resource-version.md
        └── ADR-005-dxr-scene-responsibility.md
```

### 8.1 模块依赖方向

```text
app
 ↓
renderer
 ↓
rdg
 ↓
NVRHI / Donut

renderer → raytracing
raytracing → rdg
rdg 不依赖具体 renderer pass
```

`rdg` 不能反向依赖 `GBufferPass`、`DXRPass` 等具体模块。`raytracing` 可以使用 RDG 的资源和 Pass 接口，但不能让 RDG 了解 BLAS、TLAS 或具体 Hit Group。

---

## 9. 依赖管理

### 9.1 主要依赖

| 依赖 | 用途 | 管理方式 |
|---|---|---|
| Donut | 应用、场景、Shader、基础渲染框架 | Git Submodule，固定 commit |
| NVRHI | 资源、命令、绑定、Pipeline、Ray Tracing 抽象 | 由 Donut 固定版本间接管理；记录对应 commit |
| DirectX-Headers | D3D12 类型和辅助定义 | 使用 Donut/NVRHI 所需版本 |
| DXC | HLSL Shader Model 6.6+ | 固定官方 Release/NuGet 版本 |
| D3D12 Agility SDK | D3D12 Runtime | 固定官方 NuGet 版本 |
| WinPixEventRuntime | PIX Marker | 固定官方 NuGet 版本 |
| DirectXMath | 数学 | vcpkg 或 Donut 已有版本 |
| fmt | 日志和格式化 | vcpkg |
| nlohmann/json | 配置、Benchmark 输出 | vcpkg |
| Catch2 | CPU 单元测试 | vcpkg |
| Tracy | 可选 CPU/线程分析 | 第二阶段再加入 |

### 9.2 版本原则

- Donut、NVRHI 使用固定 commit；
- 不追踪主分支作为长期依赖；
- 每次升级都保存构建环境和结果；
- `THIRD_PARTY_NOTICES.md` 记录许可证；
- 不提交外部库构建产物；
- 通过 `bootstrap.ps1` 检查依赖版本。

### 9.3 Donut Submodule 约束

- 不直接修改 Donut/NVRHI 的公共代码；
- 先通过现有接口实现 GBuffer、Post Process、RDG 和 DXR 层；
- 如果必须修改，独立提交并写 ADR；
- 不把 Donut 的现有 Render Graph 直接当作本项目 Mini RDG；
- 如果 Donut 已经提供部分 Pass 管理能力，先隔离使用，避免与自研 RDG 混用导致职责不清。

---

## 10. 实施时间线

按每周约 10～15 小时业余投入估算，整体目标为 12～16 周。每个阶段都可以独立形成结果。

### 阶段 0：仓库和框架稳定，1 周

任务：

- 创建仓库；
- 固定 Donut/NVRHI commit；
- 跑通 D3D12 示例；
- 创建自己的 `RenderingLabApp`；
- 输出 GPU、驱动、DXR Tier、Shader Model；
- 记录构建环境；
- 接入基础 ImGui 和 GPU Marker。

验收：

- 全新目录可构建；
- 能加载一个 glTF 场景；
- PIX 能捕获完整帧；
- 不修改 Donut/NVRHI 公共接口。

### 阶段 1：GBuffer 光栅化，2 周

任务：

- Scene Mesh 绘制；
- Depth Buffer；
- GBuffer0 Base Color；
- GBuffer1 Normal；
- GBuffer2 Roughness/Metallic/AO；
- Debug View；
- 基础材质和相机变换；
- GPU Timestamp。

验收：

- GBuffer 各通道可独立查看；
- 相机移动和实例变换正确；
- 深度、法线、颜色空间约定有文档；
- GBuffer Pass 可以被单独 Profile。

### 阶段 2：Deferred Shading，1～2 周

任务：

- Directional Light；
- Point Light；
- Lambert + 简化 GGX；
- HDR Scene Color；
- 光照 Debug View；
- 基础 Light Buffer。

验收：

- GBuffer → Deferred Lighting 运行稳定；
- 能验证法线、深度和材质参数读取；
- 输出 HDR Scene Color；
- 不依赖 DXR。

### 阶段 3：基础 Post Processing，1 周

任务：

- Exposure；
- Tone Mapping；
- Gamma/sRGB 输出；
- 可选 FXAA；
- Debug Overlay；
- Back Buffer Present。

验收：

```text
GBuffer
→ Deferred Lighting
→ HDR Scene Color
→ Tone Mapping
→ Back Buffer
```

整条基础光栅管线稳定运行，并具备清晰的资源访问边界。

### 阶段 4：Mini RDG 核心，2～3 周

任务顺序：

1. `RGPass` 和 `RGResource` 数据结构；
2. Pass Read/Write 声明；
3. Resource Version；
4. RAW/WAR/WAW 依赖；
5. DAG 和拓扑排序；
6. Pass Culling；
7. First/Last Use；
8. Graph Debug Dump；
9. 基础 Resource State/Barrier Planning。

迁移顺序：

1. 先把 Post Processing 接入 RDG；
2. 再把 Deferred Lighting 接入 RDG；
3. 最后把 GBuffer 接入 RDG。

验收：

- 三个基础 Pass 全部由 RDG 调度；
- Graph 可导出 DOT/文本；
- 能检测循环依赖和未声明访问；
- 可以裁剪无效 Pass；
- 结果和手动 Pass 版本一致。

### 阶段 5：RDG 生命周期和基础 Transient Resource，1～2 周

任务：

- Logical/Physical Resource 分离；
- First/Last Use；
- 资源创建延迟到 Graph Compile；
- 生命周期不重叠资源复用；
- 基础 Aliasing/Reuse 统计；
- Pass 执行上下文。

第一阶段不要求实现通用高性能 Heap Allocator。先证明生命周期分析、资源复用和访问状态正确。

验收：

- 能显示每个资源生命周期；
- 能比较独立资源和复用资源的峰值显存；
- 不影响 GBuffer、Deferred 和 Post Process 正确性。

### 阶段 6：DXR Scene 基础架构，2～3 周

任务：

- `RTGeometry`；
- `RTInstance`；
- `RayTracingScene`；
- BLAS Registry；
- 静态 BLAS；
- TLAS；
- Build/Update；
- Instance Mask；
- AS 显存和 GPU 时间统计；
- Fence 驱动的延迟资源释放。

验收：

- 一个静态场景可以生成 RT Scene；
- 动态 Transform 可以更新 TLAS；
- BLAS/TLAS 生命周期不依赖全局 GPU 等待；
- DXR Scene 可以作为 RDG Pass 的输入资源。

### 阶段 7：DXR Pipeline 和第一个 Ray Tracing Pass，2 周

任务：

- RTPSO；
- Ray Generation；
- Miss；
- Closest Hit；
- Hit Group；
- SBT；
- 最小 Payload；
- Shadow/AO/Visibility Ray；
- Ray Tracing Pass 接入 RDG。

最终 Pass：

```text
GBuffer
    ↓
Build/Update BLAS
    ↓
Build/Update TLAS
    ↓
Ray Traced Shadow/AO
    ↓
Deferred Lighting
    ↓
Post Processing
```

验收：

- Ray Tracing Pass 可被 RDG 调度；
- TLAS Build 在 Trace 前完成；
- Ray 输出可与 Deferred Lighting 合成；
- PIX 中可看到 AS、Trace 和 Post Process 时间。

### 阶段 8：DXR 架构完善，2 周

任务：

- BLAS Update vs Rebuild 开关；
- RT Geometry 动态状态；
- RT Material/Hit Group 映射；
- SBT 记录布局；
- `TraceRay` 与 Inline `RayQuery` 两条路径；
- Alpha Test/Any-Hit 可选实验；
- AS Scratch 统计；
- RT Scene Debug View。

验收：

- 可以替换 RT 几何和材质；
- 可以切换 Ray Type；
- 可以切换 Pipeline Trace 和 Inline Query；
- 各种策略有 JSON 性能结果。

### 阶段 9：性能实验与作品化，2～3 周

实验内容：

- BLAS Update vs Rebuild；
- TLAS Update vs Rebuild；
- `TraceRay` vs `RayQuery`；
- Payload 16/32/64 Bytes；
- Opaque vs Alpha Test；
- 几何粒度；
- 动态细长几何；
- 可选 NVIDIA/AMD 对照。

作品化内容：

- 架构图；
- RDG DAG；
- Resource Lifetime 图；
- DXR Scene 状态机；
- PIX/Nsight 截图；
- 性能曲线；
- 失败方案；
- 自包含静态 HTML 技术报告；
- README 和复现脚本。

---

## 11. 里程碑与停止标准

### M0：框架稳定

完成 Donut/NVRHI 应用和场景加载。若此时平台已经不稳定，不进入自研 RDG。

### M1：基础光栅渲染器

完成：

```text
GBuffer
→ Deferred Shading
→ Tone Mapping
→ Present
```

这是第一个必须保留的可展示版本。

### M2：Mini RDG

完成基础 Pass/Resource 依赖、Graph 编译、Pass Culling 和 Barrier Planning。此时已经形成一个独立的引擎架构作品。

### M3：DXR Scene

完成 RTGeometry、RTInstance、BLAS/TLAS、静态和动态场景。此时可以称为“实现了简化 DXR 场景架构”。

### M4：DXR Pass

完成一个基于 RDG 的 Shadow/AO/Visibility Ray Pass。此时可以开始投递和展示，不需要等待复杂 RTGI。

### M5：性能报告

完成至少两组可复现性能实验和一份技术报告。

### 关键停止原则

- GBuffer/Deferred/Post Process 不能无限扩展材质功能；
- RDG 不能无限复制 UE 的边界接口；
- DXR 架构不能在第一版加入完整 ReSTIR/RTGI；
- 任何新功能必须回答“是否服务于 RDG 或 DXR 架构验证”；
- 如果连续两周都在解决 Donut/NVRHI 适配问题，应缩小范围，而不是继续扩张。

---

## 12. 测试计划

### 12.1 CPU 单元测试

- Resource Version；
- RAW/WAR/WAW 依赖；
- Topological Sort；
- Cycle Detection；
- Pass Culling；
- First/Last Use；
- Transient Resource Interval；
- RTInstance Descriptor 生成；
- SBT Record Offset 和对齐；
- Hit Group 映射；
- 配置解析。

### 12.2 图形回归测试

固定：

- 场景；
- 相机；
- 分辨率；
- 光源；
- 材质；
- 随机种子。

输出：

- GBuffer 截图；
- Deferred HDR 截图；
- Tone Mapped 截图；
- Ray Traced Shadow/AO 截图；
- 可选误差图。

### 12.3 GPU Benchmark

示例：

```powershell
RenderingLab.exe `
  --mode benchmark `
  --scene scenes/cornell.gltf `
  --resolution 1920x1080 `
  --warmup-frames 120 `
  --frames 600 `
  --output results/rt-scene.json
```

至少记录：

- GPU 和驱动；
- D3D12/DXR 版本；
- 分辨率；
- GBuffer 时间；
- Deferred 时间；
- Post Process 时间；
- BLAS 时间；
- TLAS 时间；
- Ray Trace 时间；
- 总 GPU 帧时间；
- AS 显存；
- Transient Resource 峰值。

---

## 13. 主要风险和应对

### 风险一：Mini RDG 过度模仿 UE

应对：

- 只实现 Resource、Pass、Graph Compile、Culling、Barrier、Lifetime；
- 不实现 UE 的全部 Shader Parameter、View Family、平台条件和复杂 Builder API；
- 先让三个基础 Pass 跑通，再扩展。

### 风险二：基础光栅渲染陷入材质系统

应对：

- 第一版只支持固定 PBR Material；
- 只支持 glTF 基础材质；
- 不做材质编辑器、复杂 Shader Permutation 和完整纹理系统。

### 风险三：DXR 架构拖延项目

应对：

- 先完成静态三角形 BLAS/TLAS；
- 第一条 Ray 只做 Shadow/AO/Visibility；
- 不以 RTGI 作为 DXR 架构的验收条件。

### 风险四：Render Graph 与 NVRHI 状态系统重复

应对：

- RDG 只管理逻辑资源、Pass 依赖和访问意图；
- NVRHI 负责实际资源和后端状态；
- 明确谁创建、谁销毁、谁 Transition；
- 禁止某个 Pass 绕过 RDG 私自修改资源状态。

### 风险五：项目继续不可预测地膨胀

应对：

- 每个阶段都有可运行版本；
- M1 基础光栅完成即可保留成果；
- M2 Mini RDG 完成即可形成架构作品；
- M4 DXR Pass 完成即可进入作品化；
- 复杂性能实验全部放到最后。

---

## 14. 最终成果的面试表达

目标表达：

> 我基于 Donut/NVRHI 搭建了一个小型实时渲染框架，先实现了 GBuffer、Deferred Shading 和基础 Post Processing，然后自己设计并实现了一个简化版 UE 风格 RDG，支持逻辑资源、资源版本、Pass 依赖、Pass Culling、生命周期分析和基础 Barrier Planning。在此基础上，我实现了类似 UE Ray Tracing Scene 的架构，管理 RT Geometry、RT Instance、BLAS/TLAS、SBT 和 Ray Tracing Pass，并通过 D3D12 + DXR 1.1 验证了动态几何和不同 Ray 执行路径的性能。

这段经历应该重点展示：

1. 为什么先实现基础光栅管线；
2. RDG 如何解决 Pass/Resource 依赖；
3. RDG 与 NVRHI 的职责边界；
4. DXR Scene 如何与普通 Scene 和 RDG 连接；
5. BLAS/TLAS 生命周期如何管理；
6. 哪些内容是你实现的，哪些内容是 Donut/NVRHI 提供的；
7. 性能结论如何通过 PIX/Nsight 和固定 Benchmark 验证。

---

## 15. 第一周行动清单

1. 固定 Donut/NVRHI commit；
2. 创建 `RenderingLab` 仓库；
3. 跑通 Donut D3D12 示例；
4. 创建自己的应用入口；
5. 加载一个 glTF 场景；
6. 确定 GBuffer 格式和坐标/颜色空间约定；
7. 实现最小 GBuffer Pass；
8. 加入 GBuffer Debug View；
9. 加入 GPU Timestamp；
10. 写 `docs/g_buffer.md` 和 `docs/adr/ADR-001-donut-nvrhi-base.md`；
11. 暂不实现 DXR、RDG 和复杂材质；
12. 第 7 天检查是否能稳定运行 `Scene → GBuffer → Present`。

第一阶段的成功标准不是“马上看到 DXR”，而是：

```text
Donut/NVRHI 稳定运行
→ GBuffer 正确
→ Deferred Shading 可扩展
→ Post Processing 有明确边界
→ 后续可以安全抽象为 Mini RDG
```
