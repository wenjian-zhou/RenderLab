#pragma once

#include "GBufferTargets.h"
#include "HDRSceneColorTarget.h"
#include "LightingContract.h"
#include "shaders/renderer_cb.h"

#include <donut/engine/BindingCache.h>
#include <donut/engine/ShaderFactory.h>
#include <nvrhi/nvrhi.h>

#include <array>
#include <cstdint>
#include <memory>

namespace renderlab
{
    inline constexpr uint32_t kDeferredLightingTimerQueryCount = 4;
    inline constexpr uint32_t kDeferredLightingConstantBufferVersions = 16;

    // Inputs named for later RDG migration. Caller owns GBuffer and lighting CPU data.
    struct DeferredLightingPassInputs
    {
        nvrhi::ITexture* gbufferA = nullptr;
        nvrhi::ITexture* gbufferB = nullptr;
        nvrhi::ITexture* gbufferC = nullptr;
        nvrhi::ITexture* gbufferDepth = nullptr;
        const ViewConstants* viewConstants = nullptr;
        const LightingConstants* lightingConstants = nullptr;
    };

    // Output named for later RDG migration. Caller owns HDRSceneColor lifetime.
    struct DeferredLightingPassOutputs
    {
        nvrhi::ITexture* hdrSceneColor = nullptr;
    };

    struct DeferredLightingPassHud
    {
        float gpuTimeMilliseconds = 0.f;
        bool timestampValid = false;
    };

    nvrhi::RasterState MakeDeferredLightingRasterState();
    nvrhi::DepthStencilState MakeDeferredLightingDepthState();
    DeferredLightingPassInputs MakeDeferredLightingPassInputs(
        const GBufferTargets& targets,
        const ViewConstants& viewConstants,
        const LightingConstants& lightingConstants);
    DeferredLightingPassOutputs MakeDeferredLightingPassOutputs(const HDRSceneColorTarget& hdr);

    // S2.3: clear HDRSceneColor and write Lambert + GGX deferred lighting.
    // Marker: DeferredLighting. Timestamped. Fullscreen triangle; depth test/write off.
    class DeferredLightingPass
    {
    public:
        bool Init(nvrhi::IDevice* device, donut::engine::ShaderFactory& shaderFactory);
        void ReleaseSizeDependentResources();

        void Execute(
            nvrhi::ICommandList* commandList,
            const DeferredLightingPassInputs& inputs,
            const DeferredLightingPassOutputs& outputs);

        const DeferredLightingPassHud& GetHud() const { return m_hud; }

    private:
        bool EnsureFramebuffer(nvrhi::ITexture* hdrSceneColor);
        nvrhi::IBindingSet* GetOrCreateBindingSet(const DeferredLightingPassInputs& inputs);
        nvrhi::IGraphicsPipeline* GetOrCreatePipeline(nvrhi::IFramebuffer* framebuffer);

        nvrhi::DeviceHandle m_device;
        nvrhi::ShaderHandle m_vertexShader;
        nvrhi::ShaderHandle m_pixelShader;
        nvrhi::BindingLayoutHandle m_bindingLayout;
        nvrhi::BufferHandle m_viewCB;
        nvrhi::BufferHandle m_lightingCB;
        nvrhi::FramebufferHandle m_framebuffer;
        nvrhi::ITexture* m_framebufferColor = nullptr;
        nvrhi::GraphicsPipelineHandle m_pipeline;
        nvrhi::FramebufferInfo m_pipelineInfo{};
        bool m_hasPipelineInfo = false;
        std::unique_ptr<donut::engine::BindingCache> m_bindingCache;
        std::array<nvrhi::TimerQueryHandle, kDeferredLightingTimerQueryCount> m_timerQueries{};
        std::array<uint8_t, kDeferredLightingTimerQueryCount> m_timerInFlight{};
        uint32_t m_timerIndex = 0;
        DeferredLightingPassHud m_hud;
    };
}
