#pragma once

#include "GBufferTargets.h"
#include "LightingContract.h"
#include "shaders/lighting_debug_cb.h"
#include "shaders/renderer_cb.h"

#include <donut/engine/BindingCache.h>
#include <donut/engine/ShaderFactory.h>
#include <nvrhi/nvrhi.h>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace renderlab
{
    inline constexpr uint32_t kLightingDebugConstantBufferVersions = 16;

    enum class PresentSource : uint32_t
    {
        GBufferDebug = 0,
        LightingDebug = 1
    };

    // Inputs named for later RDG migration.
    // world-position / ndotl reconstruct from GBuffer; lit reads HDRSceneColor (Reinhard).
    struct LightingDebugPassInputs
    {
        nvrhi::ITexture* gbufferA = nullptr;
        nvrhi::ITexture* gbufferB = nullptr;
        nvrhi::ITexture* gbufferC = nullptr;
        nvrhi::ITexture* gbufferDepth = nullptr;
        nvrhi::ITexture* hdrSceneColor = nullptr;
        const ViewConstants* viewConstants = nullptr;
        const LightingConstants* lightingConstants = nullptr;
        LightingDebugMode mode = LightingDebugMode::Lit;
    };

    // Output named for later RDG migration. Caller owns the color target (back buffer or dump).
    struct LightingDebugPassOutputs
    {
        nvrhi::ITexture* debugColor = nullptr;
    };

    struct LightingDebugModeInfo
    {
        LightingDebugMode mode = LightingDebugMode::Lit;
        const char* cliName = "";
        const char* channelName = "";
        const char* decodeConvention = "";
        const char* dumpFileName = "";
    };

    nvrhi::RasterState MakeLightingDebugRasterState();
    nvrhi::DepthStencilState MakeLightingDebugDepthState();
    LightingDebugPassInputs MakeLightingDebugPassInputs(
        const GBufferTargets& targets,
        nvrhi::ITexture* hdrSceneColor,
        const ViewConstants& viewConstants,
        const LightingConstants& lightingConstants,
        LightingDebugMode mode);

    const LightingDebugModeInfo& GetLightingDebugModeInfo(LightingDebugMode mode);
    bool ParseLightingDebugMode(std::string_view text, LightingDebugMode& mode, std::string& error);

    // Lighting visualization pass. Marker: LightingDebug. No timestamp.
    class LightingDebugPass
    {
    public:
        bool Init(nvrhi::IDevice* device, donut::engine::ShaderFactory& shaderFactory);
        void ReleaseSizeDependentResources();

        void Execute(
            nvrhi::ICommandList* commandList,
            const LightingDebugPassInputs& inputs,
            const LightingDebugPassOutputs& outputs);

        nvrhi::ITexture* GetOrCreateDumpTarget(uint32_t width, uint32_t height);

    private:
        bool EnsureFramebuffer(nvrhi::ITexture* debugColor);
        nvrhi::IBindingSet* GetOrCreateBindingSet(const LightingDebugPassInputs& inputs);
        nvrhi::IGraphicsPipeline* GetOrCreatePipeline(nvrhi::IFramebuffer* framebuffer);

        nvrhi::DeviceHandle m_device;
        nvrhi::ShaderHandle m_vertexShader;
        nvrhi::ShaderHandle m_pixelShader;
        nvrhi::BindingLayoutHandle m_bindingLayout;
        nvrhi::BufferHandle m_viewCB;
        nvrhi::BufferHandle m_lightingCB;
        nvrhi::BufferHandle m_debugCB;
        nvrhi::FramebufferHandle m_framebuffer;
        nvrhi::ITexture* m_framebufferColor = nullptr;
        nvrhi::GraphicsPipelineHandle m_pipeline;
        nvrhi::FramebufferInfo m_pipelineInfo{};
        bool m_hasPipelineInfo = false;
        nvrhi::TextureHandle m_dumpTarget;
        std::unique_ptr<donut::engine::BindingCache> m_bindingCache;
    };
}
