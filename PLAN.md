# DXRLab 可执行路线图

## 1. 项目定位

**项目名（暂定）**：DXRLab  
**目标平台**：Windows 11、D3D12、DXR 1.1、HLSL Shader Model 6.6+、C++20  
**项目性质**：用于验证 D3D12/DXR 系统架构、Render Graph、GPU 生命周期和性能优化的轻量实验框架，不做完整游戏引擎。

优先级：

\[
\text{Correctness} > \text{Observability} > \text{Maintainability} > \text{Generality}
\]

本路线图按每周约 12～16 小时的业余投入估算，总参考工期为 **20～24 周**。阶段时间只用于容量规划；是否进入下一阶段由退出条件决定，不以日历周强行截断。范围完整性优先于固定期限，同时只允许一个阶段处于进行中。

### 范围内

- 3 Frames in Flight
- Raster GBuffer 基线
- 静态/动态 BLAS、TLAS Build/Update
- BLAS Compaction、AS Scratch Pool
- DXR Pipeline `TraceRay` 与 Inline `RayQuery`
- SBT 与 Bindless 场景数据
- 简单 1-bounce RTGI 与基础时域累积
- Render Graph：依赖、生命周期、Barrier、Transient Aliasing
- Graphics/Compute 多队列实验
- 多线程 Command List Recording
- GPU Timestamp、JSON/CSV Benchmark、PIX/NSight/RGP 分析

### 暂不包含

- 编辑器、ECS、物理、游戏逻辑
- Vulkan/Metal/主机后端和通用跨平台 RHI
- 完整动画、FBX、完整资产管线
- 完整 ReSTIR、Wavefront Path Tracer、复杂 Denoiser
- 完整商业级材质系统

---

## 2. 交付版本与边界

每项主要能力只归属于一个首次交付版本，后续版本可以增强，但不得重复声明为首次完成。

### v0.1：稳定 DXR 基线

- 可重复构建、Windows CI 与固定依赖版本
- D3D12 Core、三帧并行和 Fence 驱动的生命周期
- Raster GBuffer 与固定 glTF 测试场景
- 最小 Render Graph：Pass/Resource 声明、依赖、排序和基础自动 Barrier
- 静态 BLAS/TLAS
- Inline `RayQuery` Shadow 或 AO
- 最小 `TraceRay` Pipeline、SBT 和 Debug View
- GPU Timestamp、JSON/CSV Benchmark、PIX Capture

**明确不属于 v0.1**：动态 AS、RTGI、时域累积、BLAS Compaction、完整 Scratch Pool、Transient Aliasing、Multi-Queue、并行录制。

### v0.2：动态光追与 RTGI

- 静态/动态几何分类
- BLAS Build/Update/Rebuild 策略
- TLAS Update 与实例增删
- BLAS Compaction 与完整 AS Scratch Pool
- Fence 驱动的 AS 回收和显存统计
- 简单 1-bounce RTGI、History Validation 与时域累积
- `RayQuery`/`TraceRay` 可切换路径

### v0.3：高级内存与调度

- Transient Heap、Placed Resource 与 Aliasing Barrier
- Graphics/Compute Multi-Queue 调度与跨队列 Fence
- Worker-local Command Allocator/List 与并行录制
- 单队列/多队列、单线程/多线程对照数据

### 最终发布：性能研究与作品化

- DXR 性能实验矩阵
- NVIDIA 与 AMD GPU 架构对照分析
- Render Graph DAG、Queue Timeline 和 AS 状态机
- 自包含静态 HTML 技术报告、GPU Capture 与性能曲线
- 完整 README、构建说明和第三方许可证

---

## 3. 工具链与固定版本

| 工具 | 用途 | 管理方式 |
|---|---|---|
| Visual Studio 2022 | MSVC、调试器、Windows 开发环境 | 开发机安装，记录最低版本 |
| Windows SDK | Win32、DXGI、D3D12 基础接口 | VS Installer；在 CI 和文档固定最低版本 |
| CMake | 构建系统 | `CMakePresets.json` 固定配置 |
| Ninja | 本地/CI 构建器 | 开发机或 CI 安装 |
| vcpkg | 常规 C++ 依赖 | Manifest 模式，固定 baseline 与 triplet |
| NuGet CLI | 微软 D3D12 运行时相关包 | `bootstrap.ps1` 下载固定版本 |
| Git | 版本管理 | 主仓库 |
| GitHub Actions | Windows CI | Debug/Release 构建与 CPU 测试 |
| PIX for Windows | D3D12/DXR 主分析工具 | 开发机单独安装 |
| Nsight Graphics | NVIDIA GPU 深度分析 | 可选，开发机安装 |
| Radeon GPU Profiler | AMD RDNA 分析 | 可选，开发机安装 |
| RenderDoc | Raster 与普通 Compute 辅助调试 | 可选；DXR 分析优先使用 PIX |

阶段 1 必须在 `docs/build-environment.md` 中锁定并记录：

- Visual Studio、MSVC、Windows SDK、CMake 和 Ninja 的最低验证版本；
- vcpkg `builtin-baseline` 与唯一 Windows x64 triplet；
- DirectX-Headers/`d3dx12.h` 的唯一来源，禁止混用不匹配的头文件；
- Agility SDK、DXC、WinPixEventRuntime 和 NuGet CLI 的精确版本；
- 已验证 GPU、驱动与功能级别组合。

不得使用 `latest` 或依赖开发机上未记录的隐式版本。

---

## 4. 运行时与第三方依赖

### 4.1 D3D12 运行时依赖

| 依赖 | 用途 | 管理方式 |
|---|---|---|
| D3D12 Agility SDK | 固定现代 D3D12 Runtime | 官方 NuGet 包，锁定精确版本 |
| DXC | 编译 HLSL SM 6.6+ | 官方 NuGet/Release，锁定精确版本；封装 `IDxcCompiler3` |
| DirectX-Headers / `d3dx12.h` | D3D12 头文件和辅助结构 | 阶段 1 选定唯一来源并记录版本 |
| WinPixEventRuntime | PIX CPU/GPU Marker | 官方 NuGet 包，锁定精确版本 |
| Windows SDK / DXGI | Adapter、SwapChain、Debug Layer、DRED | 系统工具链 |
| WRL `ComPtr` | COM 对象管理 | Windows SDK 自带 |

启动时必须输出 Adapter、驱动、DXR Tier、Shader Model 和 Agility SDK 版本。`D3D12SDKVersion` 与 `D3D12SDKPath` 由程序导出，并验证 Agility SDK 运行时文件随可执行文件正确部署。

### 4.2 v0.1 必选 C++ 依赖

| 库 | 用途 | 管理方式 |
|---|---|---|
| DirectXMath | CPU 端向量、矩阵与相机数学 | vcpkg |
| D3D12 Memory Allocator | 持久 GPU 资源分配 | vcpkg |
| fastgltf | glTF 2.0 场景解析 | vcpkg |
| DirectXTex | DDS/WIC 纹理读取与转换 | vcpkg |
| Dear ImGui | 参数、Debug View、Benchmark UI | Git Submodule，固定 commit |
| fmt | 字符串格式化与轻量日志基础 | vcpkg |
| nlohmann/json | 配置与 Benchmark JSON | vcpkg |
| Catch2 | CPU 单元测试 | vcpkg |
| CLI11 | Headless、场景与 Benchmark 命令行 | vcpkg |

v0.1 结束时复核依赖使用情况：未被产品代码、测试或工具实际引用的依赖必须移出 Manifest。

### 4.3 按需引入依赖

| 库 | 用途 | 最早引入时机 |
|---|---|---|
| meshoptimizer | Mesh 优化和后续 LOD 实验 | 确认存在对应实验后 |
| tinyexr | HDR、Ground Truth 与误差图导出 | 开始图像质量实验后 |
| Tracy | CPU 线程、Job 与 Frame 流程分析 | 并行录制阶段 |
| Google Benchmark | CPU 数据结构微基准 | 确认 Catch2 计时不足后 |

禁止为了潜在需求提前加入上述依赖。

### 4.4 明确不引入的依赖

以下项目只能作为阅读或结果对照，不作为主仓库基础依赖：

- Unreal Engine 5.x
- Falcor
- NVIDIA Donut
- Microsoft MiniEngine 整套框架
- DirectXTK12 的高层渲染/资源管理封装
- 外部 Render Graph、AS Manager、SBT Manager
- 外部 ECS、完整 Job System、跨平台 RHI

原因是它们会隐藏本项目需要验证的 Descriptor、Fence、Barrier、Render Graph、BLAS/TLAS 和 SBT 生命周期。

---

## 5. 依赖与测试资产管理

```text
普通 C++ 库        → vcpkg Manifest
微软 D3D12 包      → NuGet/官方 Release + bootstrap.ps1
Dear ImGui         → Git Submodule
大型测试资产       → manifest + 下载脚本 + SHA-256
小型测试资产       → 仓库直接提交
工具软件           → 开发机/CI 安装并记录版本
```

所有依赖必须：

1. 固定版本、tag、baseline 或 commit；
2. 禁止直接提交未说明来源的二进制；
3. 在 `THIRD_PARTY_NOTICES.md` 中记录来源和许可证；
4. 通过全新目录完成可重复构建验证；
5. 更新依赖时单独提交并执行完整回归。

### 5.1 vcpkg Manifest

阶段 1 生成真实、无占位符的 `vcpkg.json`。优先验证 `x64-windows-static-md`；若任一必选依赖不兼容，则统一使用 `x64-windows`。同一构建不得混用 triplet。

### 5.2 微软 NuGet 包

```text
external/
└── nuget/
    ├── agility-sdk/
    ├── dxc/
    └── pix-runtime/
```

`scripts/bootstrap.ps1` 负责下载固定版本 NuGet CLI 和三个微软包、校验必要文件并生成 `external/versions.json`。包目录不得提交进 Git。CMake 分别通过以下模块集中处理 include、lib、DLL 复制与安装规则：

- `cmake/AgilitySDK.cmake`
- `cmake/DXC.cmake`
- `cmake/WinPixEventRuntime.cmake`

### 5.3 测试资产

```text
assets/
├── builtin/             # 小型、可提交的测试资产
├── downloaded/          # .gitignore
└── manifest.json        # URL、许可证、SHA-256、用途
```

`scripts/fetch_assets.ps1` 根据 manifest 下载并校验。最低资产集合包括：

- 最小三角形/立方体场景；
- 固定 Cornell Box 或类似小型 GI 场景；
- Alpha Test 场景；
- 动态顶点替代测试场景；
- 大量实例场景。

Hair、Procedural Geometry 和大型公开场景仅在对应实验开始时加入。

---

## 6. 仓库结构与自研边界

```text
DXRLab/
├── CMakeLists.txt
├── CMakePresets.json
├── vcpkg.json
├── README.md
├── LICENSE
├── THIRD_PARTY_NOTICES.md
├── cmake/
├── external/
├── src/
│   ├── app/
│   ├── core/
│   ├── platform/win32/
│   ├── gfx/d3d12/
│   ├── render_graph/
│   ├── raytracing/
│   ├── renderer/
│   └── benchmark/
├── shaders/
├── tests/
├── assets/
├── scripts/
└── docs/
    ├── vision.md
    ├── architecture.md
    ├── build-environment.md
    └── adr/
```

### 必须自行实现

```text
D3D12 Core
├── Device / Adapter / Feature Check
├── Queue / Fence / Frame Context
├── Command Allocator/List Pool
├── Persistent/Transient Descriptor
├── Upload/Readback Ring
└── Deferred Deletion

Render Graph
├── Pass/Resource 声明与 Resource Versioning
├── RAW/WAR/WAW 依赖与拓扑排序
├── Pass Culling 与 Lifetime Analysis
├── Barrier Planning
├── Transient Aliasing
├── Queue Scheduling
└── Parallel Recording Batch

DXR Framework
├── RT Geometry Registry
├── BLAS/TLAS Manager
├── AS Scratch Pool 与 Compaction
├── Build/Update/Rebuild Policy
├── RTPSO / SBT / Inline RayQuery
└── Fence 驱动的 AS 回收
```

Render Graph 必须渐进实现：v0.1 只包含声明、依赖、排序、Pass Culling、First/Last Use 和基础 Barrier；v0.3 才加入 Transient Aliasing、跨队列调度和并行录制 Batch。

---

## 7. 工程与生命周期规范

### 编译配置

- C++20
- `/W4 /permissive-`
- Debug/Release 两套 Preset
- 可选 AddressSanitizer 的 CPU 工具测试配置
- Shader Debug/Release 分开编译
- 所有 D3D12 对象设置 Debug Name

### 图形约定

阶段 1 在 `docs/architecture.md` 固定：

- 左手或右手坐标；
- D3D NDC 深度范围 \([0,1]\)；
- 是否 Reversed-Z；
- CPU/HLSL 矩阵行列约定；
- Front Face；
- UV 与纹理原点；
- 颜色空间、HDR 与 Pre-exposure 约定。

### 调试配置

Debug 默认开启 D3D12 Debug Layer、DXGI Debug、DRED Breadcrumb 与 Page Fault Reporting。InfoQueue 遇到 Error/Corruption 必须中断；Warning 需要分类处理，已知且确认无害的 Warning 才能在文档中按 ID 记录。GPU-Based Validation 由命令行开关启用，不参与性能测量。

### 生命周期规则

- 可能仍被未完成 GPU 工作引用的 Resource 必须通过 Deferred Deletion 回收；
- 可能仍被未完成 GPU 工作引用的 Descriptor 必须按对应 Queue Fence 延迟复用；
- 尚未提交、创建失败或在确认所有相关 Queue idle 后的对象允许立即释放；
- Command Allocator 仅在对应 Queue Fence 完成后 Reset；
- 三帧并行不得依赖每帧 CPU 等待 GPU 来掩盖生命周期问题；
- Graphics、Compute、Copy Fence 分别记录，不直接比较不同 Fence 的数值。

---

## 8. 八阶段实施路线

### 阶段 1：项目约定与可重复构建（约 1 周）

**交付内容**：

- 创建目录、`vision.md`、`architecture.md` 和 `build-environment.md`
- 锁定工具、vcpkg baseline/triplet、头文件来源与微软包精确版本
- 配置 CMake、Presets、vcpkg Manifest、clang-format 和 `.gitignore`
- 编写 `bootstrap.ps1` 与 Windows GitHub Actions
- 运行 Win32 空窗口并输出构建与运行时版本

**退出条件**：

- 全新 clone 后可用一条脚本拉取依赖、配置、编译并运行空窗口；
- Debug/Release CI 构建与 CPU 测试入口成功；
- 文档和 `external/versions.json` 中不存在版本占位符。

**失败处理**：任一依赖无法在统一 triplet 下构建时，先收敛 triplet 或移除非必需依赖，不允许同时维护两套未经验证的依赖组合。

### 阶段 2：D3D12 Core 与三帧生命周期（约 2 周）

**交付内容**：

- Adapter 枚举、Feature Check、Device、Direct Queue 与 SwapChain
- 3 个 Frame Context、Fence、Command Allocator/List Pool
- RTV/DSV Heap、Debug Layer、InfoQueue 与 DRED
- 基础 Deferred Deletion 与 Handle/Fence Retirement CPU 测试

**退出条件**：

- 稳定 Present，CPU 不做每帧强制等待；
- Debug Layer 无 Error/Corruption；
- Command Allocator 只在对应 Fence 完成后 Reset；
- 连续运行和窗口 Resize 不出现资源生命周期错误。

**失败处理**：若三帧并行不稳定，保留最小复现并修正 Fence 所有权，禁止退回每帧 `WaitForGpu` 作为最终方案。

### 阶段 3：资源系统、可观测性与最小 Render Graph（约 2 周）

**交付内容**：

- Persistent/Transient Descriptor、Upload Ring、Readback 与 Deferred Deletion
- DXC Shader 编译、PIX Marker、GPU Timestamp 和 ImGui
- Render Graph Pass/Resource、Resource Versioning、RAW/WAR/WAW、拓扑排序
- Pass Culling、First/Last Use、基础 Transition/UAV Barrier Planning
- Render Graph DOT 导出和 CPU 单元测试

**退出条件**：

- 测试覆盖 Descriptor 分配/回收、Fence Retirement、依赖、排序和 Pass Culling；
- PIX 中能清晰识别 Frame/Pass，UI 能显示 GPU 时间；
- 最小 Graphics/Compute 测试 Pass 由 Graph 调度且 Debug Layer 无 Error/Corruption。

**失败处理**：若自动 Barrier 语义尚不完整，限制 v0.1 支持的 `RGAccess` 集合并使未支持组合显式失败，不允许静默退化为未知状态。

### 阶段 4：Raster、glTF 与固定测试场景（约 2 周）

**交付内容**：

- fastgltf 场景加载与 DirectXTex 纹理加载
- 基础 Mesh/Material Buffer、Depth/GBuffer
- 简单 Direct Lighting、Tone Mapping
- 固定场景、相机、分辨率、随机种子与参考截图
- GBuffer、Lighting、Composite 全部接入 Render Graph

**退出条件**：

- 稳定加载最小场景与固定 Cornell 场景；
- 基础 PBR 数据、坐标和颜色空间符合架构约定；
- 同一 GPU/驱动下参考图像差异处于规定容差内；
- Headless 模式能输出截图和非零失败退出码。

**失败处理**：资产不支持时输出带资源路径和原因的结构化错误；不得用缺失纹理或默认材质静默掩盖验收场景错误。

### 阶段 5：静态 DXR 纵向切片与 v0.1（约 3 周）

**交付内容**：

- DXR 1.1 与 SM 6.6 Feature Check
- 静态 BLAS/TLAS 最小实现
- Inline `RayQuery` Shadow 或 AO
- RTPSO、RayGen、Miss、Closest-Hit、Hit Group 与最小 Payload
- SBT Record 布局、32/64-byte 对齐和 Bindless Geometry/Material 索引
- AS Build-to-Trace 同步、Timestamp、Debug View 与 Benchmark

**退出条件**：

- Raster、`RayQuery` 和 `TraceRay` 路径可以切换；
- `TraceRay` 能输出法线、Instance ID、Primitive ID Debug View；
- SBT Offset/Stride/Alignment 有 CPU 测试；
- 固定场景在 GPU Validation 下无 Error/Corruption；
- 发布 v0.1，并附 README、PIX Capture 与可重复 Benchmark 结果。

**失败处理**：设备不支持 DXR 1.1 或 SM 6.6 时，以非零退出码和能力报告结束；不得回退到不同功能路径后仍报告 DXR 测试成功。

### 阶段 6：动态 AS、RTGI 与 v0.2（约 4 周）

**交付内容**：

- 静态/动态几何分类和 Build/Update/Rebuild 策略
- BLAS Compaction、完整 Scratch Pool、TLAS Update 与实例增删
- AS Fence 延迟释放和显存统计
- 1-bounce RTGI、History Validation、时域累积与 Composite
- `RayQuery`/`TraceRay` 可选路径

**退出条件**：

- 动态几何连续更新稳定，不无意义地每帧重建所有 AS；
- Build/Update/Rebuild、Compacted/Non-compacted 有可重复对照数据；
- `GBuffer → TLAS → RTGI → Temporal → Composite` 全流程由 Graph 调度；
- Camera Cut、Resize、场景变化会使无效 History 明确失效；
- 发布 v0.2 并记录 AS 状态机和显存统计。

**失败处理**：更新前提不满足时必须显式选择 Rebuild；Scratch 或 Compaction 资源不足时报告请求大小与当前容量，不允许覆盖仍在使用的内存。

### 阶段 7：高级内存、调度与 v0.3（约 4 周）

**交付内容**：

- Transient Heap Page、Placed Resource、Lifetime Interval Packing 与 Aliasing Barrier
- Compute Queue、跨 Queue 依赖与 Fence
- Direct-only/Direct+Compute 两种执行模式
- Thread Pool、Worker-local Command Allocator/List 与 Recording Batch
- GPU Timeline、CPU Frame Time 和峰值显存统计

**退出条件**：

- 独立分配与 Aliasing 图像结果在容差内一致，并报告峰值显存变化；
- 单队列与多队列报告同步、带宽竞争和关键路径，不以出现重叠代替帧时间收益；
- 单线程与多线程录制结果一致，无共享可变状态或 Allocator Reset 错误；
- 发布 v0.3，并保留可切换的对照路径。

**失败处理**：若高级路径没有收益或不稳定，保留正确的基线路径并如实记录负结果，不以复杂度作为保留理由。

### 阶段 8：性能研究与作品化（约 4～6 周）

**交付内容**：

- `TraceRay` vs Inline `RayQuery`
- Payload 16/32/64 Bytes
- BLAS/TLAS Update vs Rebuild
- Compacted vs Non-compacted BLAS
- Opaque vs Alpha Test、相干 vs 非相干 Ray
- Roughness、Ray Length、Rays Per Pixel 参数矩阵
- NVIDIA Warp32 与 AMD Wave32/Wave64 对照分析
- 架构图、AS 状态机、DAG、Queue Timeline 和静态 HTML 技术报告

**退出条件**：

- 每组实验记录场景、相机、随机种子、分辨率、Ray 数、GPU、驱动与统计方法；
- 至少完成 NVIDIA/AMD 两类 GPU 的部分实验，无法获得硬件时明确标注限制；
- 区分可推广结论与单硬件结论，不虚构跨架构结果；
- 面试官可在 1 分钟内看到结果、10 分钟内理解架构，并可继续查看源码和原始数据。

**失败处理**：受硬件或工具限制的矩阵项标记为“未测”并说明原因，不得用推测值或不同配置结果补齐表格。

---

## 9. 验证与 Benchmark 合同

### 9.1 CPU 单元测试

- Handle Pool 与 Generation
- Descriptor 分配/回收
- Fence Retirement
- Render Graph DAG、拓扑排序与 Pass Culling
- RAW/WAR/WAW 与 Resource Lifetime
- SBT Record Offset、Stride 和 Alignment
- Transient Interval Packing

GitHub Actions 托管 runner 负责 Debug/Release 构建和 CPU 测试。普通 GitHub 托管 runner 不承担 DXR 正确性或性能结论；GPU Smoke、GPU Validation 和 Benchmark 在本机或具备固定 GPU/驱动的自托管 runner 上执行。

### 9.2 GPU 正确性验证

每个正式场景必须固定：

- 资产版本与 SHA-256；
- 相机变换、分辨率、随机种子和渲染参数；
- GPU、驱动、Agility SDK、DXC 版本；
- 允许的图像绝对/相对误差阈值。

同一 GPU/驱动组合使用带容差的图像差异检查；跨 GPU 不要求字节哈希一致，而是使用更宽松且有记录的数值/感知阈值。Debug Layer 的 Error/Corruption 数量必须为零；Warning 必须被处理或按 ID 记录理由。

### 9.3 Benchmark 协议

性能运行必须：

- 关闭 VSync、Debug Layer、GPU-Based Validation 和外部 Capture；
- 固定场景、相机、随机种子、分辨率、Ray 数与所有质量参数；
- 默认 warm-up 120 帧、采样 600 帧、独立重复 5 次；
- 报告每次运行及汇总的 median、P95，置信区间方法在报告中固定；
- 明确区分 CPU Frame、GPU Frame/Critical Path 和 Present Interval；
- 发生资产、设备、Shader、验证或输出错误时返回非零退出码。

建议命令：

```powershell
DXRLab.exe `
  --scene assets/downloaded/cornell/cornell.gltf `
  --headless `
  --width 1920 `
  --height 1080 `
  --seed 1 `
  --warmup-frames 120 `
  --frames 600 `
  --repeat 5 `
  --vsync off `
  --output results/cornell.json
```

JSON 至少包含：

```json
{
  "schema_version": 1,
  "status": "ok",
  "run_config": {
    "scene": "cornell",
    "resolution": [1920, 1080],
    "seed": 1,
    "warmup_frames": 120,
    "sample_frames": 600,
    "repeat_count": 5,
    "vsync": false,
    "validation": false,
    "rays_per_pixel": 1.0
  },
  "system": {
    "gpu": "GPU Name",
    "driver": "Driver Version",
    "agility_sdk": "SDK Version"
  },
  "metrics_ms": {
    "cpu_frame_median": 0.0,
    "cpu_frame_p95": 0.0,
    "gpu_frame_median": 0.0,
    "gpu_frame_p95": 0.0,
    "blas_build_median": 0.0,
    "tlas_build_median": 0.0,
    "trace_median": 0.0,
    "temporal_median": 0.0,
    "composite_median": 0.0
  },
  "memory_mb": {
    "as": 0.0,
    "transient_peak": 0.0
  }
}
```

---

## 10. 提交与架构决策纪律

每个主要功能分为三类提交：

1. **Bring-up**：最小正确实现；
2. **Validation**：Debug Layer、测试、截图与 Benchmark；
3. **Optimization/Refactor**：有数据支持后再优化或抽象。

每个架构决策写入 `docs/adr/`，至少包括：

```text
ADR-001: Why D3D12-only and no generic RHI
ADR-002: Persistent vs Transient Descriptors
ADR-003: Fence-based Deferred Deletion
ADR-004: Semantic RGAccess and Barrier Backend
ADR-005: Bindless SBT Record Layout
ADR-006: BLAS Update/Rebuild Policy
```

依赖更新必须独立提交。阶段验收失败时先提交最小复现或诊断材料，再修复；不得把临时 CPU/GPU 全局等待、关闭验证层或扩大图像容差作为最终修复。

---

## 11. 项目成功标准

项目成功不以功能数量衡量，而以能否用代码、Capture 和数据清楚回答以下问题衡量：

- 资源何时创建、使用和回收？
- BLAS/TLAS 为什么选择 Build、Update 或 Rebuild？
- Barrier 和 Queue Fence 为什么这样生成？
- `TraceRay` 与 `RayQuery` 的性能边界在哪里？
- 优化收益来自 Traversal、Shader、内存、发散还是调度？
- 结论在 Warp32 与 Wave32/Wave64 架构上是否一致？

最终发布必须同时具备：可重复构建、可复现图像、结构化 Benchmark、验证过的 GPU Capture、明确的硬件限制和不夸大的技术结论。
