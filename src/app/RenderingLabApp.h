#pragma once

#include "SceneCatalog.h"
#include "renderer/DeferredLightingPass.h"
#include "renderer/GBufferDebugPass.h"
#include "renderer/GBufferPass.h"
#include "renderer/LightingDebugPass.h"
#include "renderer/LightingContract.h"
#include "renderer/PostProcessPass.h"
#include "renderer/RendererData.h"
#include "rdg/exec/ExecGraph.h"
#include "rdg/exec/GraphExecutor.h"

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

    struct GBufferDebugHud
    {
        GBufferDebugMode mode = GBufferDebugMode::BaseColor;
        const char* channelName = "";
        const char* decodeConvention = "";
        std::string dumpDirectory;
        bool dumpRequested = false;
        bool dumpCompleted = false;
        bool dumpSucceeded = false;
        uint32_t dumpCount = 0;
    };

    struct LightingDebugHud
    {
        LightingDebugMode mode = LightingDebugMode::Lit;
        const char* channelName = "";
        const char* decodeConvention = "";
        std::string dumpDirectory;
        bool dumpRequested = false;
        bool dumpCompleted = false;
        bool dumpSucceeded = false;
        uint32_t dumpCount = 0;
    };

    // Non-owning textures from the frame that just executed. Valid until the
    // next RenderScene or BackBufferResizing drops the executor.
    struct RasterFrameSnapshot
    {
        nvrhi::ITexture* gbufferA = nullptr;
        nvrhi::ITexture* gbufferB = nullptr;
        nvrhi::ITexture* gbufferC = nullptr;
        nvrhi::ITexture* gbufferDepth = nullptr;
        nvrhi::ITexture* hdrSceneColor = nullptr;
        uint32_t width = 0;
        uint32_t height = 0;
        bool valid = false;
        bool hasHdr = false;
    };

    struct AppLaunchOptions
    {
        ResolvedScene scene;
        CameraPreset camera;
        bool lockCamera = false;
        bool verifyLights = false;
        PresentSource presentSource = kDefaultPresentSource;
        GBufferDebugMode gbufferView = GBufferDebugMode::BaseColor;
        LightingDebugMode lightingView = LightingDebugMode::Lit;
        float exposureEV = kDefaultExposureEV;
        std::string dumpGBufferViewsDirectory;
        std::string dumpLightingViewsDirectory;
        std::string goldenOutputDirectory;
        bool writeCaptureMetadata = false;
        std::string hdrOutputDirectory;
        bool writeHdrCaptureMetadata = false;
    };

    class RenderingLabUserInterface final : public donut::app::ImGui_Renderer
    {
    public:
        RenderingLabUserInterface(
            donut::app::DeviceManager* deviceManager,
            const DeviceCapabilities& capabilities,
            const SceneHudState& sceneHud,
            const RasterFrameSnapshot& frameSnapshot,
            const GBufferPassHud& gbufferPassHud,
            const DeferredLightingPassHud& deferredLightingHud,
            const PostProcessPassHud& postProcessHud,
            PresentSource& presentSource,
            GBufferDebugHud& gbufferDebugHud,
            LightingDebugHud& lightingDebugHud);

    protected:
        void buildUI() override;
        void Render(nvrhi::IFramebuffer* framebuffer) override;

    private:
        DeviceCapabilities m_capabilities;
        const SceneHudState& m_sceneHud;
        const RasterFrameSnapshot& m_frameSnapshot;
        const GBufferPassHud& m_gbufferPassHud;
        const DeferredLightingPassHud& m_deferredLightingHud;
        const PostProcessPassHud& m_postProcessHud;
        PresentSource& m_presentSource;
        GBufferDebugHud& m_gbufferDebugHud;
        LightingDebugHud& m_lightingDebugHud;
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
        const RasterFrameSnapshot& GetFrameSnapshot() const;
        const GBufferPassHud& GetGBufferPassHud() const;
        const DeferredLightingPassHud& GetDeferredLightingPassHud() const;
        const PostProcessPassHud& GetPostProcessPassHud() const;
        PresentSource& GetPresentSource();
        const PresentSource& GetPresentSource() const;
        GBufferDebugHud& GetGBufferDebugHud();
        const GBufferDebugHud& GetGBufferDebugHud() const;
        LightingDebugHud& GetLightingDebugHud();
        const LightingDebugHud& GetLightingDebugHud() const;
        bool DumpGBufferDebugViews(const std::string& directory);
        bool DumpLightingDebugViews(const std::string& directory);
        bool DumpHdrCapture(const std::string& directory);
        bool HdrDumpCompleted() const;
        bool HdrDumpSucceeded() const;
        bool WriteCaptureMetadata(const std::string& directory, uint32_t frameIndex) const;
        bool WriteHdrCaptureMetadata(const std::string& directory, uint32_t frameIndex, uint64_t nonFiniteCount) const;

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
        void UpdateDebugHud();
        void ReleaseFrameGraph();
        void ExecuteRasterFrame(nvrhi::ITexture* backBuffer);
        void LogRasterFrameErrors() const;

        struct RasterFrameHandles
        {
            rdg::TextureHandle gbufferA;
            rdg::TextureHandle gbufferB;
            rdg::TextureHandle gbufferC;
            rdg::TextureHandle gbufferDepth;
            rdg::TextureHandle hdrWritten;
            rdg::TextureHandle outputWritten;
        };

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
        LightingConstants m_lightingConstants = {};
        GBufferPass m_gbufferPass;
        DeferredLightingPass m_deferredLightingPass;
        GBufferDebugPass m_gbufferDebugPass;
        LightingDebugPass m_lightingDebugPass;
        PostProcessPass m_postProcessPass;
        PresentSource m_presentSource = kDefaultPresentSource;
        TonemapConstants m_tonemapConstants = MakeDefaultTonemapConstants();
        GBufferDebugHud m_gbufferDebugHud;
        LightingDebugHud m_lightingDebugHud;
        bool m_hdrDumpCompleted = false;
        bool m_hdrDumpSucceeded = false;
        uint32_t m_backBufferWidth = 0;
        uint32_t m_backBufferHeight = 0;
        std::unique_ptr<rdg::exec::ExecGraph> m_frameGraph;
        std::unique_ptr<rdg::exec::GraphExecutor> m_frameExecutor;
        RasterFrameHandles m_frameHandles;
        RasterFrameSnapshot m_frameSnapshot;
        bool m_stopRasterFrame = false;
        bool m_framePresented = false;
    };
}
