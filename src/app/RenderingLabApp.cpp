#include "RenderingLabApp.h"

#include <donut/app/DeviceManager.h>
#include <donut/core/log.h>
#include <donut/core/vfs/VFS.h>
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
        const DeviceCapabilities& capabilities)
        : ImGui_Renderer(deviceManager)
        , m_capabilities(capabilities)
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
        ImGui::SetNextWindowSize(ImVec2(420.0f, 0.0f), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("RenderLab"))
        {
            ImGui::End();
            return;
        }

        ImGui::TextUnformatted("S0.3 baseline");
        ImGui::Separator();
        ImGui::Text("Adapter: %s", m_capabilities.adapterName.c_str());
        ImGui::Text("Driver: %s", m_capabilities.driverVersion.c_str());
        ImGui::Text("NVRHI backend: %s", m_capabilities.nvrhiBackend.c_str());
        ImGui::Text("DXR tier: %s", m_capabilities.dxrTier.c_str());
        ImGui::Text("Shader model: %s", m_capabilities.shaderModel.c_str());
        ImGui::Text("Resolution: %d x %d", width, height);
        ImGui::Text("Frame: %u", deviceManager->GetFrameIndex());
        ImGui::End();
    }

    RenderingLabApp::RenderingLabApp(app::DeviceManager* deviceManager)
        : ApplicationBase(deviceManager)
    {
        SetAsynchronousLoadingEnabled(false);
    }

    bool RenderingLabApp::Init()
    {
        QueryCapabilities();
        PrintDeviceCapabilities(m_capabilities);

        m_commandList = GetDevice()->createCommandList();
        if (!m_commandList)
        {
            log::error("Failed to create the NVRHI command list.");
            return false;
        }

        return true;
    }

    const DeviceCapabilities& RenderingLabApp::GetCapabilities() const
    {
        return m_capabilities;
    }

    void RenderingLabApp::Render(nvrhi::IFramebuffer* framebuffer)
    {
        m_commandList->open();
        nvrhi::utils::ClearColorAttachment(m_commandList, framebuffer, 0, kClearColor);
        m_commandList->close();
        GetDevice()->executeCommandList(m_commandList);
    }

    void RenderingLabApp::Animate(float elapsedTimeSeconds)
    {
        (void)elapsedTimeSeconds;
        GetDeviceManager()->SetInformativeWindowTitle("RenderLab");
    }

    void RenderingLabApp::BackBufferResized(
        const uint32_t width,
        const uint32_t height,
        const uint32_t sampleCount)
    {
        (void)sampleCount;
        m_backBufferWidth = width;
        m_backBufferHeight = height;
        log::info("Back buffer resized to %u x %u", width, height);
    }

    bool RenderingLabApp::LoadScene(
        std::shared_ptr<vfs::IFileSystem> fs,
        const std::filesystem::path& sceneFileName)
    {
        (void)fs;
        (void)sceneFileName;
        log::error("--scene is not implemented (scheduled for S0.4).");
        return false;
    }

    bool RenderingLabApp::ShouldRenderUnfocused()
    {
        return true;
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
