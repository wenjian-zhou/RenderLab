#pragma once

#include "GBufferTargets.h"
#include "shaders/gbuffer_debug_cb.h"
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
    inline constexpr uint32_t kGBufferDebugConstantBufferVersions = 16;

    // Inputs named for later RDG migration. GBufferA/B/C and GBufferDepth are reads.
    struct GBufferDebugPassInputs
    {
        nvrhi::ITexture* gbufferA = nullptr;
        nvrhi::ITexture* gbufferB = nullptr;
        nvrhi::ITexture* gbufferC = nullptr;
        nvrhi::ITexture* gbufferDepth = nullptr;
        const ViewConstants* viewConstants = nullptr;
        GBufferDebugMode mode = GBufferDebugMode::BaseColor;
    };

    // Output named for later RDG migration. Caller owns the color target (back buffer or dump).
    struct GBufferDebugPassOutputs
    {
        nvrhi::ITexture* debugColor = nullptr;
    };

    struct GBufferDebugModeInfo
    {
        GBufferDebugMode mode = GBufferDebugMode::BaseColor;
        const char* cliName = "";
        const char* channelName = "";
        const char* decodeConvention = "";
        const char* dumpFileName = "";
    };

    nvrhi::RasterState MakeGBufferDebugRasterState();
    nvrhi::DepthStencilState MakeGBufferDebugDepthState();
    GBufferDebugPassInputs MakeGBufferDebugPassInputs(
        const GBufferTargets& targets,
        const ViewConstants& viewConstants,
        GBufferDebugMode mode);

    const GBufferDebugModeInfo& GetGBufferDebugModeInfo(GBufferDebugMode mode);
    bool ParseGBufferDebugMode(std::string_view text, GBufferDebugMode& mode, std::string& error);
    bool LinearizeViewDepth(float deviceDepth, float zNear, float& viewZ);
    float LinearDepthDisplayValue(float viewZ);

    class GBufferDebugPass
    {
    public:
        bool Init(nvrhi::IDevice* device, donut::engine::ShaderFactory& shaderFactory);
        void ReleaseSizeDependentResources();

        void Execute(
            nvrhi::ICommandList* commandList,
            const GBufferDebugPassInputs& inputs,
            const GBufferDebugPassOutputs& outputs);

        nvrhi::ITexture* GetOrCreateDumpTarget(uint32_t width, uint32_t height);

    private:
        bool EnsureFramebuffer(nvrhi::ITexture* debugColor);
        nvrhi::IBindingSet* GetOrCreateBindingSet(const GBufferDebugPassInputs& inputs);
        nvrhi::IGraphicsPipeline* GetOrCreatePipeline(nvrhi::IFramebuffer* framebuffer);

        nvrhi::DeviceHandle m_device;
        nvrhi::ShaderHandle m_vertexShader;
        nvrhi::ShaderHandle m_pixelShader;
        nvrhi::BindingLayoutHandle m_bindingLayout;
        nvrhi::BufferHandle m_viewCB;
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
