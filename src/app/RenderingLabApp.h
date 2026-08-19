#pragma once

#include "SceneCatalog.h"
#include "renderer/GBufferTargets.h"
#include "renderer/RendererData.h"

#include <donut/app/ApplicationBase.h>
#include <donut/app/Camera.h>
#include <donut/app/imgui_renderer.h>
#include <donut/engine/Scene.h>
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
        std::string validationMode = "off";
        bool dxrSupported = false;
        bool nvrhiValidation = false;
        bool d3d12DebugRuntime = false;
        bool gpuBasedValidation = false;
    };

    struct SceneHudState
    {
        std::string sceneId;
        std::string sceneLabel;
        std::string license;
        uint32_t meshCount = 0;
        uint32_t instanceCount = 0;
        uint32_t materialCount = 0;
        uint32_t drawCount = 0;
        uint32_t skippedDrawCount = 0;
        donut::math::float3 cameraPosition = 0.f;
        donut::math::float3 cameraDirection = 0.f;
        donut::math::float3 cameraTarget = 0.f;
        float verticalFovDegrees = 45.0f;
        bool cameraLocked = false;
        bool sceneLoaded = false;
    };

    struct AppLaunchOptions
    {
        ResolvedScene scene;
        CameraPreset camera;
        bool lockCamera = false;
    };

    class RenderingLabUserInterface final : public donut::app::ImGui_Renderer
    {
    public:
        RenderingLabUserInterface(
            donut::app::DeviceManager* deviceManager,
            const DeviceCapabilities& capabilities,
            const SceneHudState& sceneHud,
            const GBufferTargets& gbuffer);

    protected:
        void buildUI() override;
        void Render(nvrhi::IFramebuffer* framebuffer) override;

    private:
        DeviceCapabilities m_capabilities;
        const SceneHudState& m_sceneHud;
        const GBufferTargets& m_gbuffer;
    };

    int SelectPreferredAdapterIndex(const std::vector<donut::app::AdapterInfo>& adapters);
    void PrintDeviceCapabilities(const DeviceCapabilities& capabilities);

    class RenderingLabApp final : public donut::app::ApplicationBase
    {
    public:
        RenderingLabApp(
            donut::app::DeviceManager* deviceManager,
            std::shared_ptr<donut::engine::ShaderFactory> shaderFactory,
            std::shared_ptr<donut::vfs::IFileSystem> fileSystem,
            AppLaunchOptions options);

        bool Init();
        const DeviceCapabilities& GetCapabilities() const;
        const SceneHudState& GetSceneHud() const;
        const GBufferTargets& GetGBufferTargets() const;

        void RenderScene(nvrhi::IFramebuffer* framebuffer) override;
        void RenderSplashScreen(nvrhi::IFramebuffer* framebuffer) override;
        void Animate(float elapsedTimeSeconds) override;
        void BackBufferResizing() override;
        void BackBufferResized(const uint32_t width, const uint32_t height, const uint32_t sampleCount) override;
        bool LoadScene(std::shared_ptr<donut::vfs::IFileSystem> fs, const std::filesystem::path& sceneFileName) override;
        void SceneLoaded() override;
        bool ShouldRenderUnfocused() override;
        bool KeyboardUpdate(int key, int scancode, int action, int mods) override;
        bool MousePosUpdate(double xpos, double ypos) override;
        bool MouseButtonUpdate(int button, int action, int mods) override;
        bool MouseScrollUpdate(double xoffset, double yoffset) override;

    private:
        void QueryCapabilities();
        void ApplyCameraPreset();
        void UpdateSceneHud();
        void ClearBackBuffer(nvrhi::IFramebuffer* framebuffer);
        void RebuildDrawList();
        void UpdateFrameViewConstants();

        std::shared_ptr<donut::engine::ShaderFactory> m_shaderFactory;
        std::shared_ptr<donut::vfs::IFileSystem> m_fileSystem;
        AppLaunchOptions m_options;
        std::shared_ptr<donut::engine::Scene> m_scene;
        donut::app::FirstPersonCamera m_camera;
        nvrhi::CommandListHandle m_commandList;
        DeviceCapabilities m_capabilities;
        SceneHudState m_sceneHud;
        SceneDrawList m_drawList;
        FrameConstants m_frameConstants = {};
        ViewConstants m_viewConstants = {};
        GBufferTargets m_gbuffer;
        uint32_t m_backBufferWidth = 0;
        uint32_t m_backBufferHeight = 0;
    };
}
