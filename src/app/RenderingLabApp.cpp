#include "RenderingLabApp.h"
#include "FrameMarkers.h"
#include "renderer/GBufferPass.h"
#include "renderer/GBufferTargets.h"
#include "renderer/RendererData.h"

#include <donut/app/DeviceManager.h>
#include <donut/core/log.h>
#include <donut/core/vfs/VFS.h>
#include <donut/engine/CommonRenderPasses.h>
#include <donut/engine/SceneGraph.h>
#include <donut/engine/TextureCache.h>
#include <nvrhi/utils.h>

#include <cstdio>
#include <cstring>

#if DONUT_WITH_DX12
#include <dxgi.h>
#include <nvrhi/d3d12.h>
#endif

using namespace donut;

namespace renderlab
{
    namespace
    {
        const nvrhi::Color kClearColor(0.08f, 0.09f, 0.12f, 1.0f);

        const char* GraphicsApiName(nvrhi::GraphicsAPI api)
        {
            return nvrhi::utils::GraphicsAPIToString(api);
        }

        bool IsSoftwareOrVirtualAdapter(const app::AdapterInfo& adapter)
        {
            if (adapter.vendorID == 0x1414)
            {
                return true;
            }

            const std::string& name = adapter.name;
            return name.find("GameViewer") != std::string::npos ||
                   name.find("Virtual Display") != std::string::npos ||
                   name.find("Basic Render") != std::string::npos ||
                   name.find("Microsoft Basic") != std::string::npos;
        }

#if DONUT_WITH_DX12
        std::string FormatUmdVersion(LARGE_INTEGER version)
        {
            char buffer[64] = {};
            std::snprintf(
                buffer,
                sizeof(buffer),
                "%u.%u.%u.%u",
                static_cast<unsigned>(HIWORD(version.HighPart)),
                static_cast<unsigned>(LOWORD(version.HighPart)),
                static_cast<unsigned>(HIWORD(version.LowPart)),
                static_cast<unsigned>(LOWORD(version.LowPart)));
            return buffer;
        }

        std::string FormatShaderModel(D3D_SHADER_MODEL model)
        {
            const unsigned packed = static_cast<unsigned>(model);
            char buffer[16] = {};
            std::snprintf(buffer, sizeof(buffer), "%u.%u", packed >> 4, packed & 0xFu);
            return buffer;
        }

        std::string FormatRaytracingTier(D3D12_RAYTRACING_TIER tier)
        {
            switch (tier)
            {
            case D3D12_RAYTRACING_TIER_NOT_SUPPORTED:
                return "not supported";
            case D3D12_RAYTRACING_TIER_1_0:
                return "1.0";
            case D3D12_RAYTRACING_TIER_1_1:
                return "1.1";
#ifdef D3D12_RAYTRACING_TIER_1_2
            case D3D12_RAYTRACING_TIER_1_2:
                return "1.2";
#endif
            default:
                return "unknown";
            }
        }
#endif
    }

    int SelectPreferredAdapterIndex(const std::vector<app::AdapterInfo>& adapters)
    {
        int bestIndex = -1;
        uint64_t bestMemory = 0;
        int bestNvidiaIndex = -1;
        uint64_t bestNvidiaMemory = 0;

        for (int index = 0; index < static_cast<int>(adapters.size()); ++index)
        {
            const app::AdapterInfo& adapter = adapters[static_cast<size_t>(index)];
            if (IsSoftwareOrVirtualAdapter(adapter))
            {
                log::info(
                    "Skipping software or virtual adapter %d: %s",
                    index,
                    adapter.name.c_str());
                continue;
            }

            if (adapter.dedicatedVideoMemory >= bestMemory)
            {
                bestMemory = adapter.dedicatedVideoMemory;
                bestIndex = index;
            }

            if (adapter.vendorID == 0x10DE && adapter.dedicatedVideoMemory >= bestNvidiaMemory)
            {
                bestNvidiaMemory = adapter.dedicatedVideoMemory;
                bestNvidiaIndex = index;
            }
        }

        if (bestNvidiaIndex >= 0)
        {
            return bestNvidiaIndex;
        }

        return bestIndex;
    }

    void PrintDeviceCapabilities(const DeviceCapabilities& capabilities)
    {
        log::info("Adapter: %s", capabilities.adapterName.c_str());
        log::info("Driver version: %s", capabilities.driverVersion.c_str());
        log::info("NVRHI backend: %s", capabilities.nvrhiBackend.c_str());
        log::info("Validation: %s", capabilities.validationMode.c_str());
        log::info("DXR tier: %s", capabilities.dxrTier.c_str());
        log::info("Shader model: %s", capabilities.shaderModel.c_str());

        if (!capabilities.dxrSupported)
        {
            log::warning(
                "DXR is not supported on this adapter. RenderLab will continue with the raster path.");
        }
    }

    RenderingLabUserInterface::RenderingLabUserInterface(
        app::DeviceManager* deviceManager,
        const DeviceCapabilities& capabilities,
        const SceneHudState& sceneHud,
        const GBufferTargets& gbuffer,
        const GBufferPassHud& gbufferPassHud)
        : ImGui_Renderer(deviceManager)
        , m_capabilities(capabilities)
        , m_sceneHud(sceneHud)
        , m_gbuffer(gbuffer)
        , m_gbufferPassHud(gbufferPassHud)
    {
        ImGui::GetIO().IniFilename = nullptr;
    }

    void RenderingLabUserInterface::buildUI()
    {
        app::DeviceManager* deviceManager = GetDeviceManager();
        int width = 0;
        int height = 0;
        deviceManager->GetWindowDimensions(width, height);

        ImGui::SetNextWindowPos(ImVec2(16.0f, 16.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(460.0f, 0.0f), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("RenderLab Diagnostics"))
        {
            ImGui::End();
            return;
        }

        ImGui::TextUnformatted("S1.4 opaque GBuffer pass");
        ImGui::Separator();
        ImGui::Text("Adapter: %s", m_capabilities.adapterName.c_str());
        ImGui::Text("Driver: %s", m_capabilities.driverVersion.c_str());
        ImGui::Text("NVRHI backend: %s", m_capabilities.nvrhiBackend.c_str());
        ImGui::Text("Validation: %s", m_capabilities.validationMode.c_str());
        ImGui::Text("Resolution: %d x %d", width, height);
        ImGui::Text("Frame: %u", deviceManager->GetFrameIndex());
        ImGui::Text("DXR tier: %s", m_capabilities.dxrTier.c_str());
        ImGui::Text("Shader model: %s", m_capabilities.shaderModel.c_str());
        ImGui::Separator();

        const GBufferTargetsHud gbufferHud = m_gbuffer.GetHud();
        ImGui::TextUnformatted("GBuffer");
        if (!gbufferHud.valid)
        {
            ImGui::TextUnformatted("  not created");
        }
        else
        {
            ImGui::Text("  %u x %u, samples=%u, mips=1", gbufferHud.width, gbufferHud.height, gbufferHud.sampleCount);
            for (const GBufferTargetHud& target : gbufferHud.targets)
            {
                ImGui::Text(
                    "  %s  %s  %u B/px  %llu bytes",
                    target.debugName,
                    target.formatName,
                    target.bytesPerPixel,
                    static_cast<unsigned long long>(target.approximateBytes));
            }
            ImGui::Text(
                "  total ~%llu bytes  creates=%u releases=%u",
                static_cast<unsigned long long>(gbufferHud.approximateBytes),
                gbufferHud.createCount,
                gbufferHud.releaseCount);
            ImGui::Text("  opaque draws=%u skippedBuffers=%u", m_gbufferPassHud.drawCount, m_gbufferPassHud.skippedMissingBufferCount);
            if (m_gbufferPassHud.timestampValid)
            {
                ImGui::Text("  GPU time=%.3f ms", m_gbufferPassHud.gpuTimeMilliseconds);
            }
            else
            {
                ImGui::TextUnformatted("  GPU time=pending");
            }
        }

        ImGui::Separator();
        ImGui::Text("Scene: %s", m_sceneHud.sceneLabel.c_str());
        ImGui::Text("Meshes: %u", m_sceneHud.meshCount);
        ImGui::Text("Instances: %u", m_sceneHud.instanceCount);
        ImGui::Text("Materials: %u", m_sceneHud.materialCount);
        ImGui::Text("Draws: %u", m_sceneHud.drawCount);
        ImGui::Text("Skipped draws: %u", m_sceneHud.skippedDrawCount);
        ImGui::Text(
            "Camera: (%.3f, %.3f, %.3f)",
            m_sceneHud.cameraPosition.x,
            m_sceneHud.cameraPosition.y,
            m_sceneHud.cameraPosition.z);
        ImGui::Text(
            "Look at: (%.3f, %.3f, %.3f)",
            m_sceneHud.cameraTarget.x,
            m_sceneHud.cameraTarget.y,
            m_sceneHud.cameraTarget.z);
        ImGui::Text("Locked: %s", m_sceneHud.cameraLocked ? "true" : "false");
        ImGui::End();
    }

    void RenderingLabUserInterface::Render(nvrhi::IFramebuffer* framebuffer)
    {
        markers::CpuMarker uiMarker(GetDevice(), markers::kUI);
        ImGui_Renderer::Render(framebuffer);
    }

    RenderingLabApp::RenderingLabApp(
        app::DeviceManager* deviceManager,
        std::shared_ptr<engine::ShaderFactory> shaderFactory,
        std::shared_ptr<vfs::IFileSystem> fileSystem,
        AppLaunchOptions options)
        : ApplicationBase(deviceManager)
        , m_shaderFactory(std::move(shaderFactory))
        , m_fileSystem(std::move(fileSystem))
        , m_options(std::move(options))
    {
        SetAsynchronousLoadingEnabled(false);
        m_camera.SetMoveSpeed(3.0f);
        m_camera.SetRotateSpeed(0.005f);
        ApplyCameraPreset();
        m_sceneHud.sceneId = m_options.scene.id;
        m_sceneHud.sceneLabel = m_options.scene.virtualPath;
        m_sceneHud.license = m_options.scene.license;
        m_sceneHud.cameraLocked = m_options.lockCamera;
        m_sceneHud.cameraTarget = m_options.camera.target;
        m_sceneHud.verticalFovDegrees = m_options.camera.verticalFovDegrees;
    }

    bool RenderingLabApp::Init()
    {
        QueryCapabilities();
        PrintDeviceCapabilities(m_capabilities);
        markers::PrintMarkerNames();

        if (!m_shaderFactory || !m_fileSystem)
        {
            log::error("Scene loading requires a ShaderFactory and VFS.");
            return false;
        }

        m_CommonPasses = std::make_shared<engine::CommonRenderPasses>(GetDevice(), m_shaderFactory);
        m_TextureCache = std::make_shared<engine::TextureCache>(GetDevice(), m_fileSystem, nullptr);

        m_commandList = GetDevice()->createCommandList();
        if (!m_commandList)
        {
            log::error("Failed to create the NVRHI command list.");
            return false;
        }

        std::string layoutError;
        if (!ValidateRendererDataLayout(layoutError))
        {
            log::error("Renderer data layout is invalid: %s", layoutError.c_str());
            return false;
        }

        if (!m_gbufferPass.Init(GetDevice(), *m_shaderFactory))
        {
            log::error("Failed to initialize the opaque GBuffer pass.");
            return false;
        }

        BeginLoadingScene(m_fileSystem, m_options.scene.virtualPath);
        if (!IsSceneLoaded() || !m_scene)
        {
            log::error("Failed to load scene '%s'.", m_options.scene.virtualPath.c_str());
            return false;
        }

        return true;
    }

    const DeviceCapabilities& RenderingLabApp::GetCapabilities() const
    {
        return m_capabilities;
    }

    const SceneHudState& RenderingLabApp::GetSceneHud() const
    {
        return m_sceneHud;
    }

    const GBufferTargets& RenderingLabApp::GetGBufferTargets() const
    {
        return m_gbuffer;
    }

    const GBufferPassHud& RenderingLabApp::GetGBufferPassHud() const
    {
        return m_gbufferPass.GetHud();
    }

    void RenderingLabApp::ClearBackBuffer(nvrhi::IFramebuffer* framebuffer)
    {
        markers::CpuMarker renderMarker(GetDevice(), markers::kRender);
        m_commandList->open();
        {
            markers::GpuMarker frameMarker(m_commandList, markers::kFrame);
            markers::GpuMarker renderGpuMarker(m_commandList, markers::kRender);
            nvrhi::utils::ClearColorAttachment(m_commandList, framebuffer, 0, kClearColor);
        }
        m_commandList->close();
        GetDevice()->executeCommandList(m_commandList);
    }

    void RenderingLabApp::RenderSplashScreen(nvrhi::IFramebuffer* framebuffer)
    {
        ClearBackBuffer(framebuffer);
    }

    void RenderingLabApp::RenderScene(nvrhi::IFramebuffer* framebuffer)
    {
        markers::CpuMarker renderMarker(GetDevice(), markers::kRender);
        m_commandList->open();
        {
            markers::GpuMarker frameMarker(m_commandList, markers::kFrame);
            {
                markers::GpuMarker sceneUpdateMarker(m_commandList, markers::kSceneUpdate);
                if (m_scene)
                {
                    m_scene->Refresh(m_commandList, GetFrameIndex());
                    RebuildDrawList();
                    UpdateFrameViewConstants();
                }
            }
            {
                markers::GpuMarker renderGpuMarker(m_commandList, markers::kRender);
                GBufferPassInputs gbufferInputs;
                gbufferInputs.sceneDraws = &m_drawList;
                gbufferInputs.frameConstants = &m_frameConstants;
                gbufferInputs.viewConstants = &m_viewConstants;
                const GBufferPassOutputs gbufferOutputs = MakeGBufferPassOutputs(m_gbuffer);
                m_gbufferPass.Execute(m_commandList, gbufferInputs, gbufferOutputs);
                nvrhi::utils::ClearColorAttachment(m_commandList, framebuffer, 0, kClearColor);
            }
        }
        m_commandList->close();
        GetDevice()->executeCommandList(m_commandList);
    }

    void RenderingLabApp::Animate(float elapsedTimeSeconds)
    {
        markers::CpuMarker sceneUpdateMarker(GetDevice(), markers::kSceneUpdate);
        if (m_options.lockCamera)
        {
            ApplyCameraPreset();
        }
        else
        {
            m_camera.Animate(elapsedTimeSeconds);
        }

        UpdateSceneHud();
        UpdateFrameViewConstants();
        GetDeviceManager()->SetInformativeWindowTitle("RenderLab");
    }

    void RenderingLabApp::BackBufferResizing()
    {
        // Drop the GBuffer framebuffer first, then size-dependent texture handles, before
        // DeviceManager_DX12::ResizeSwapChain waits for idle and runs NVRHI garbage collection.
        m_gbufferPass.ReleaseSizeDependentResources();
        m_gbuffer.Release();
    }

    void RenderingLabApp::BackBufferResized(
        const uint32_t width,
        const uint32_t height,
        const uint32_t sampleCount)
    {
        m_backBufferWidth = width;
        m_backBufferHeight = height;
        UpdateFrameViewConstants();

        if (width == 0 || height == 0)
        {
            log::info("Back buffer minimized or zero-sized (%u x %u); GBuffer not recreated.", width, height);
            return;
        }

        if (sampleCount != kGBufferSampleCount)
        {
            log::warning(
                "Swap-chain sample count is %u; GBuffer stays at sample count %u (no MSAA).",
                sampleCount,
                kGBufferSampleCount);
        }

        if (!m_gbuffer.Create(GetDevice(), width, height))
        {
            log::error("Failed to create GBuffer targets at %u x %u.", width, height);
            return;
        }

        log::info("Back buffer resized to %u x %u", width, height);
    }

    bool RenderingLabApp::LoadScene(
        std::shared_ptr<vfs::IFileSystem> fs,
        const std::filesystem::path& sceneFileName)
    {
        const std::string virtualPath = sceneFileName.generic_string();
        log::info("Loading scene '%s'", virtualPath.c_str());

        auto scene = std::make_shared<engine::Scene>(
            GetDevice(),
            *m_shaderFactory,
            fs,
            m_TextureCache,
            nullptr,
            nullptr);

        if (!scene->Load(sceneFileName))
        {
            log::error("Donut failed to load scene '%s'.", virtualPath.c_str());
            return false;
        }

        m_scene = std::move(scene);
        return true;
    }

    void RenderingLabApp::SceneLoaded()
    {
        ApplicationBase::SceneLoaded();
        if (!m_scene)
        {
            return;
        }

        m_scene->FinishedLoading(GetFrameIndex());
        ApplyCameraPreset();
        RebuildDrawList();
        UpdateFrameViewConstants();
        UpdateSceneHud();

        log::info(
            "Loaded scene '%s' (%s)",
            m_options.scene.virtualPath.c_str(),
            m_options.scene.displayName.c_str());
        if (!m_options.scene.license.empty())
        {
            log::info("Scene license: %s", m_options.scene.license.c_str());
        }
        for (const DiagnosticMessage& message : m_drawList.messages)
        {
            log::warning("%s", message.text.c_str());
        }
        log::info(
            "Scene contents: meshes=%u instances=%u materials=%u opaqueDraws=%u skipped=%u",
            m_sceneHud.meshCount,
            m_sceneHud.instanceCount,
            m_sceneHud.materialCount,
            m_sceneHud.drawCount,
            m_sceneHud.skippedDrawCount);
        for (const DrawRecord& draw : m_drawList.draws)
        {
            const donut::math::float3 translation(
                draw.instance.matLocalToWorld[3][0],
                draw.instance.matLocalToWorld[3][1],
                draw.instance.matLocalToWorld[3][2]);
            log::info(
                "  draw[%u] mesh='%s' material='%s' baseColor=(%.3f, %.3f, %.3f) "
                "roughness=%.3f metallic=%.3f aoStrength=%.3f flags=0x%x "
                "translation=(%.3f, %.3f, %.3f) indices=%u",
                draw.drawIndex,
                draw.meshName.c_str(),
                draw.materialName.c_str(),
                draw.material.baseColorFactor.x,
                draw.material.baseColorFactor.y,
                draw.material.baseColorFactor.z,
                draw.material.roughness,
                draw.material.metallic,
                draw.material.occlusionStrength,
                draw.material.flags,
                translation.x,
                translation.y,
                translation.z,
                draw.geometry.indexCount);
        }
        log::info(
            "Camera preset '%s': position=(%.3f, %.3f, %.3f) target=(%.3f, %.3f, %.3f) "
            "up=(%.3f, %.3f, %.3f) vfov=%.3f deg znear=%.3f locked=%s",
            m_options.camera.name.c_str(),
            m_options.camera.position.x,
            m_options.camera.position.y,
            m_options.camera.position.z,
            m_options.camera.target.x,
            m_options.camera.target.y,
            m_options.camera.target.z,
            m_options.camera.up.x,
            m_options.camera.up.y,
            m_options.camera.up.z,
            m_options.camera.verticalFovDegrees,
            m_options.camera.zNear,
            m_options.lockCamera ? "true" : "false");
        log::info(
            "View constants: mirrored=%s fov=%.5f rad zNear=%.3f viewport=%.0fx%.0f",
            (m_viewConstants.flags & RendererViewFlag_Mirrored) ? "true" : "false",
            m_viewConstants.verticalFovRadians,
            m_viewConstants.zNear,
            m_viewConstants.viewportSize.x,
            m_viewConstants.viewportSize.y);
    }

    bool RenderingLabApp::ShouldRenderUnfocused()
    {
        return true;
    }

    bool RenderingLabApp::KeyboardUpdate(int key, int scancode, int action, int mods)
    {
        if (m_options.lockCamera)
        {
            return false;
        }
        m_camera.KeyboardUpdate(key, scancode, action, mods);
        return false;
    }

    bool RenderingLabApp::MousePosUpdate(double xpos, double ypos)
    {
        if (m_options.lockCamera)
        {
            return false;
        }
        m_camera.MousePosUpdate(xpos, ypos);
        return false;
    }

    bool RenderingLabApp::MouseButtonUpdate(int button, int action, int mods)
    {
        if (m_options.lockCamera)
        {
            return false;
        }
        m_camera.MouseButtonUpdate(button, action, mods);
        return false;
    }

    bool RenderingLabApp::MouseScrollUpdate(double xoffset, double yoffset)
    {
        (void)xoffset;
        (void)yoffset;
        return false;
    }

    void RenderingLabApp::ApplyCameraPreset()
    {
        m_camera.LookAt(m_options.camera.position, m_options.camera.target, m_options.camera.up);
    }

    void RenderingLabApp::RebuildDrawList()
    {
        if (!m_scene || !m_scene->GetSceneGraph())
        {
            m_drawList = {};
            return;
        }
        m_drawList = BuildSceneDrawList(*m_scene->GetSceneGraph());
    }

    void RenderingLabApp::UpdateFrameViewConstants()
    {
        m_frameConstants = MakeFrameConstants(GetFrameIndex());

        uint32_t width = m_backBufferWidth;
        uint32_t height = m_backBufferHeight;
        if (width == 0 || height == 0)
        {
            int windowWidth = 0;
            int windowHeight = 0;
            GetDeviceManager()->GetWindowDimensions(windowWidth, windowHeight);
            if (windowWidth > 0 && windowHeight > 0)
            {
                width = static_cast<uint32_t>(windowWidth);
                height = static_cast<uint32_t>(windowHeight);
            }
        }

        ViewFillDesc desc;
        desc.worldToView = m_camera.GetWorldToViewMatrix();
        desc.cameraPosition = m_camera.GetPosition();
        desc.verticalFovDegrees = m_options.camera.verticalFovDegrees;
        desc.zNear = m_options.camera.zNear;
        desc.viewportWidth = static_cast<float>(width);
        desc.viewportHeight = static_cast<float>(height);
        m_viewConstants = MakeViewConstants(desc);
    }

    void RenderingLabApp::UpdateSceneHud()
    {
        m_sceneHud.sceneId = m_options.scene.id;
        m_sceneHud.sceneLabel = m_options.scene.virtualPath;
        m_sceneHud.license = m_options.scene.license;
        m_sceneHud.cameraLocked = m_options.lockCamera;
        m_sceneHud.cameraPosition = m_camera.GetPosition();
        m_sceneHud.cameraDirection = m_camera.GetDir();
        m_sceneHud.cameraTarget = m_options.camera.target;
        m_sceneHud.verticalFovDegrees = m_options.camera.verticalFovDegrees;
        m_sceneHud.sceneLoaded = m_scene != nullptr;

        if (!m_scene || !m_scene->GetSceneGraph())
        {
            m_sceneHud.meshCount = 0;
            m_sceneHud.instanceCount = 0;
            m_sceneHud.materialCount = 0;
            m_sceneHud.drawCount = 0;
            m_sceneHud.skippedDrawCount = 0;
            return;
        }

        const auto graph = m_scene->GetSceneGraph();
        m_sceneHud.meshCount = static_cast<uint32_t>(graph->GetMeshes().size());
        m_sceneHud.instanceCount = static_cast<uint32_t>(graph->GetMeshInstances().size());
        m_sceneHud.materialCount = static_cast<uint32_t>(graph->GetMaterials().size());
        m_sceneHud.drawCount = static_cast<uint32_t>(m_drawList.draws.size());
        m_sceneHud.skippedDrawCount = m_drawList.skippedCount;
    }

    void RenderingLabApp::QueryCapabilities()
    {
        app::DeviceManager* deviceManager = GetDeviceManager();
        nvrhi::IDevice* device = GetDevice();

        m_capabilities.adapterName = deviceManager->GetRendererString();
        m_capabilities.nvrhiBackend = GraphicsApiName(deviceManager->GetGraphicsAPI());
        m_capabilities.driverVersion = "unavailable";
        m_capabilities.dxrTier = "not supported";
        m_capabilities.shaderModel = "unknown";
        m_capabilities.dxrSupported = false;

        const app::DeviceCreationParameters& deviceParams = deviceManager->GetDeviceParams();
        m_capabilities.nvrhiValidation = deviceParams.enableNvrhiValidationLayer;
        m_capabilities.d3d12DebugRuntime = deviceParams.enableDebugRuntime;
        m_capabilities.gpuBasedValidation = deviceParams.enableGPUValidation;
        if (m_capabilities.nvrhiValidation && m_capabilities.d3d12DebugRuntime)
        {
            m_capabilities.validationMode = "NVRHI + D3D12 debug runtime";
        }
        else if (m_capabilities.nvrhiValidation)
        {
            m_capabilities.validationMode = "NVRHI";
        }
        else if (m_capabilities.d3d12DebugRuntime)
        {
            m_capabilities.validationMode = "D3D12 debug runtime";
        }
        else
        {
            m_capabilities.validationMode = "off";
        }
        if (m_capabilities.gpuBasedValidation)
        {
            m_capabilities.validationMode += " + GPU-based validation";
        }

        const bool nvrhiDxr10 = device->queryFeatureSupport(nvrhi::Feature::RayTracingAccelStruct) &&
                                device->queryFeatureSupport(nvrhi::Feature::RayTracingPipeline);
        const bool nvrhiDxr11 = device->queryFeatureSupport(nvrhi::Feature::RayQuery);
        if (nvrhiDxr11)
        {
            m_capabilities.dxrSupported = true;
            m_capabilities.dxrTier = "1.1";
        }
        else if (nvrhiDxr10)
        {
            m_capabilities.dxrSupported = true;
            m_capabilities.dxrTier = "1.0";
        }

#if DONUT_WITH_DX12
        ID3D12Device* d3d12Device = device->getNativeObject(nvrhi::ObjectTypes::D3D12_Device);
        if (!d3d12Device)
        {
            log::warning("Could not query the native D3D12 device from NVRHI.");
            return;
        }

        D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5 = {};
        if (SUCCEEDED(d3d12Device->CheckFeatureSupport(
                D3D12_FEATURE_D3D12_OPTIONS5,
                &options5,
                sizeof(options5))))
        {
            m_capabilities.dxrTier = FormatRaytracingTier(options5.RaytracingTier);
            m_capabilities.dxrSupported = options5.RaytracingTier != D3D12_RAYTRACING_TIER_NOT_SUPPORTED;
        }

        const D3D_SHADER_MODEL candidates[] = {
#ifdef D3D_SHADER_MODEL_6_9
            D3D_SHADER_MODEL_6_9,
#endif
#ifdef D3D_SHADER_MODEL_6_8
            D3D_SHADER_MODEL_6_8,
#endif
            D3D_SHADER_MODEL_6_7,
            D3D_SHADER_MODEL_6_6,
            D3D_SHADER_MODEL_6_5,
            D3D_SHADER_MODEL_6_4,
            D3D_SHADER_MODEL_6_3,
            D3D_SHADER_MODEL_6_2,
            D3D_SHADER_MODEL_6_1,
            D3D_SHADER_MODEL_6_0,
        };

        for (D3D_SHADER_MODEL candidate : candidates)
        {
            D3D12_FEATURE_DATA_SHADER_MODEL shaderModel = {};
            shaderModel.HighestShaderModel = candidate;
            if (SUCCEEDED(d3d12Device->CheckFeatureSupport(
                    D3D12_FEATURE_SHADER_MODEL,
                    &shaderModel,
                    sizeof(shaderModel))))
            {
                m_capabilities.shaderModel = FormatShaderModel(shaderModel.HighestShaderModel);
                break;
            }
        }

        const LUID adapterLuid = d3d12Device->GetAdapterLuid();
        std::vector<app::AdapterInfo> adapters;
        if (!deviceManager->EnumerateAdapters(adapters))
        {
            return;
        }

        for (const app::AdapterInfo& adapter : adapters)
        {
            if (!adapter.luid.has_value() ||
                std::memcmp(adapter.luid->data(), &adapterLuid, sizeof(adapterLuid)) != 0)
            {
                continue;
            }

            m_capabilities.adapterName = adapter.name;
            if (adapter.dxgiAdapter)
            {
                LARGE_INTEGER umdVersion = {};
                if (SUCCEEDED(adapter.dxgiAdapter->CheckInterfaceSupport(__uuidof(IDXGIDevice), &umdVersion)))
                {
                    m_capabilities.driverVersion = FormatUmdVersion(umdVersion);
                }
            }
            break;
        }
#endif
    }
}
