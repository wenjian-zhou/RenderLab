#pragma once

#include <donut/app/ApplicationBase.h>
#include <donut/app/imgui_renderer.h>
#include <donut/engine/ShaderFactory.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace renderlab
{
    struct DeviceCapabilities
    {
        std::string adapterName = "unknown";
        std::string driverVersion = "unavailable";
        std::string nvrhiBackend = "D3D12";
        std::string dxrTier = "not supported";
        std::string shaderModel = "unknown";
        bool dxrSupported = false;
    };

    class RenderingLabUserInterface final : public donut::app::ImGui_Renderer
    {
    public:
        RenderingLabUserInterface(donut::app::DeviceManager* deviceManager, const DeviceCapabilities& capabilities);

    protected:
        void buildUI() override;

    private:
        DeviceCapabilities m_capabilities;
    };

    int SelectPreferredAdapterIndex(const std::vector<donut::app::AdapterInfo>& adapters);
    void PrintDeviceCapabilities(const DeviceCapabilities& capabilities);

    class RenderingLabApp final : public donut::app::ApplicationBase
    {
    public:
        explicit RenderingLabApp(donut::app::DeviceManager* deviceManager);

        bool Init();
        const DeviceCapabilities& GetCapabilities() const;

        void Render(nvrhi::IFramebuffer* framebuffer) override;
        void Animate(float elapsedTimeSeconds) override;
        void BackBufferResized(const uint32_t width, const uint32_t height, const uint32_t sampleCount) override;
        bool LoadScene(std::shared_ptr<donut::vfs::IFileSystem> fs, const std::filesystem::path& sceneFileName) override;
        bool ShouldRenderUnfocused() override;

    private:
        void QueryCapabilities();

        nvrhi::CommandListHandle m_commandList;
        DeviceCapabilities m_capabilities;
        uint32_t m_backBufferWidth = 0;
        uint32_t m_backBufferHeight = 0;
    };
}
