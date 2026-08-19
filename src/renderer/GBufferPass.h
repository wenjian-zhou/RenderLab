#pragma once

#include "GBufferTargets.h"
#include "RendererData.h"

#include <donut/engine/BindingCache.h>
#include <donut/engine/ShaderFactory.h>
#include <nvrhi/nvrhi.h>

#include <array>
#include <cstdint>
#include <memory>

namespace renderlab
{
    inline constexpr uint32_t kGBufferTimerQueryCount = 4;
    inline constexpr uint32_t kGBufferConstantBufferVersions = 16;
    inline constexpr uint32_t kGBufferDummyVertexMinimum = 65536;

    // Inputs named for later RDG migration. Materials and instance transforms live on
    // each DrawRecord; shaders consume renderer_cb.h, not Donut PlanarViewConstants.
    struct GBufferPassInputs
    {
        const SceneDrawList* sceneDraws = nullptr;
        const FrameConstants* frameConstants = nullptr;
        const ViewConstants* viewConstants = nullptr;
    };

    // Outputs named for later RDG migration. Caller owns texture lifetime (S1.3).
    struct GBufferPassOutputs
    {
        nvrhi::ITexture* gbufferA = nullptr;
        nvrhi::ITexture* gbufferB = nullptr;
        nvrhi::ITexture* gbufferC = nullptr;
        nvrhi::ITexture* gbufferDepth = nullptr;
    };

    struct GBufferRasterKey
    {
        bool frontCounterClockwise = true;
        bool twoSided = false;
    };

    struct GBufferPassHud
    {
        uint32_t drawCount = 0;
        uint32_t skippedMissingBufferCount = 0;
        float gpuTimeMilliseconds = 0.f;
        bool timestampValid = false;
    };

    nvrhi::RasterState MakeGBufferRasterState(const GBufferRasterKey& key);
    nvrhi::DepthStencilState MakeGBufferDepthState();
    GBufferPassOutputs MakeGBufferPassOutputs(const GBufferTargets& targets);

    class GBufferPass
    {
    public:
        bool Init(nvrhi::IDevice* device, donut::engine::ShaderFactory& shaderFactory);
        void ReleaseSizeDependentResources();

        void Execute(
            nvrhi::ICommandList* commandList,
            const GBufferPassInputs& inputs,
            const GBufferPassOutputs& outputs);

        const GBufferPassHud& GetHud() const { return m_hud; }

    private:
        struct PipelineKey
        {
            uint32_t value = 0;

            static PipelineKey From(bool frontCounterClockwise, bool twoSided)
            {
                PipelineKey key;
                key.value = (frontCounterClockwise ? 1u : 0u) | (twoSided ? 2u : 0u);
                return key;
            }
        };

        bool CreateFallbackTextures();
        bool EnsureDummyVertexCapacity(nvrhi::ICommandList* commandList, uint32_t vertexCount);
        bool EnsureFramebuffer(const GBufferPassOutputs& outputs);
        nvrhi::IBindingSet* GetOrCreateBindingSet(const DrawRecord& draw);
        nvrhi::IGraphicsPipeline* GetOrCreatePipeline(PipelineKey key, nvrhi::IFramebuffer* framebuffer);

        nvrhi::DeviceHandle m_device;
        nvrhi::ShaderHandle m_vertexShader;
        nvrhi::ShaderHandle m_pixelShader;
        nvrhi::InputLayoutHandle m_inputLayout;
        nvrhi::BindingLayoutHandle m_bindingLayout;
        nvrhi::BufferHandle m_frameCB;
        nvrhi::BufferHandle m_viewCB;
        nvrhi::BufferHandle m_instanceCB;
        nvrhi::BufferHandle m_materialCB;
        nvrhi::SamplerHandle m_sampler;
        nvrhi::TextureHandle m_fallbackWhiteSrgb;
        nvrhi::TextureHandle m_fallbackWhite;
        nvrhi::TextureHandle m_fallbackFlatNormal;
        nvrhi::TextureHandle m_fallbackOcclusion;
        nvrhi::BufferHandle m_dummyTexCoord;
        nvrhi::BufferHandle m_dummyTangent;
        uint32_t m_dummyVertexCount = 0;
        nvrhi::FramebufferHandle m_framebuffer;
        nvrhi::ITexture* m_framebufferA = nullptr;
        nvrhi::ITexture* m_framebufferB = nullptr;
        nvrhi::ITexture* m_framebufferC = nullptr;
        nvrhi::ITexture* m_framebufferDepth = nullptr;
        std::array<nvrhi::GraphicsPipelineHandle, 4> m_pipelines{};
        std::unique_ptr<donut::engine::BindingCache> m_bindingCache;
        std::array<nvrhi::TimerQueryHandle, kGBufferTimerQueryCount> m_timerQueries{};
        std::array<uint8_t, kGBufferTimerQueryCount> m_timerInFlight{};
        uint32_t m_timerIndex = 0;
        GBufferPassHud m_hud;
    };
}
