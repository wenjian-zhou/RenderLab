#pragma once

#include "PostProcessContract.h"

#include <donut/engine/BindingCache.h>
#include <donut/engine/ShaderFactory.h>
#include <nvrhi/nvrhi.h>

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace renderlab
{
    inline constexpr uint32_t kPostProcessTimerQueryCount = 4;
    inline constexpr uint32_t kPostProcessConstantBufferVersions = 16;

    // Tone-mapped capture artifact written next to hdr-scene-color.rlhdr
    // (docs/postprocess.md section 9: display-referred LDR golden).
    inline constexpr const char* kFinalImageFileName = "final.png";

    // Inputs named for later RDG migration. The pass only reads HDRSceneColor.
    struct PostProcessPassInputs
    {
        nvrhi::ITexture* hdrSceneColor = nullptr;
        const TonemapConstants* tonemapConstants = nullptr;
    };

    // Output named for later RDG migration. Caller owns the color target
    // (back buffer or SRGBA8 dump target).
    struct PostProcessPassOutputs
    {
        nvrhi::ITexture* finalColor = nullptr;
    };

    struct PostProcessPassHud
    {
        float gpuTimeMilliseconds = 0.f;
        bool timestampValid = false;
    };

    nvrhi::RasterState MakePostProcessRasterState();
    nvrhi::DepthStencilState MakePostProcessDepthState();
    PostProcessPassInputs MakePostProcessPassInputs(
        nvrhi::ITexture* hdrSceneColor,
        const TonemapConstants& tonemapConstants);

    // Parses --exposure-ev values: any finite float, no clamp
    // (docs/postprocess.md section 3). Non-finite text (nan / inf / overflow)
    // is malformed input, not a clamped value.
    bool ParseExposureEv(std::string_view text, float& exposureEV, std::string& error);

    // S3.2 exposure + tone map (docs/postprocess.md). Marker: PostProcess.
    // Timestamped. Fullscreen triangle; depth test/write off; reads
    // HDRSceneColor and never writes it. Writes display-referred linear values;
    // the SRGBA8_UNORM target applies the only output transfer.
    class PostProcessPass
    {
    public:
        bool Init(nvrhi::IDevice* device, donut::engine::ShaderFactory& shaderFactory);
        void ReleaseSizeDependentResources();

        void Execute(
            nvrhi::ICommandList* commandList,
            const PostProcessPassInputs& inputs,
            const PostProcessPassOutputs& outputs);

        const PostProcessPassHud& GetHud() const { return m_hud; }

        nvrhi::ITexture* GetOrCreateDumpTarget(uint32_t width, uint32_t height);

    private:
        bool EnsureFramebuffer(nvrhi::ITexture* finalColor);
        nvrhi::IBindingSet* GetOrCreateBindingSet(const PostProcessPassInputs& inputs);
        nvrhi::IGraphicsPipeline* GetOrCreatePipeline(nvrhi::IFramebuffer* framebuffer);

        nvrhi::DeviceHandle m_device;
        nvrhi::ShaderHandle m_vertexShader;
        nvrhi::ShaderHandle m_pixelShader;
        nvrhi::BindingLayoutHandle m_bindingLayout;
        nvrhi::BufferHandle m_tonemapCB;
        nvrhi::FramebufferHandle m_framebuffer;
        nvrhi::ITexture* m_framebufferColor = nullptr;
        nvrhi::GraphicsPipelineHandle m_pipeline;
        nvrhi::FramebufferInfo m_pipelineInfo{};
        bool m_hasPipelineInfo = false;
        nvrhi::TextureHandle m_dumpTarget;
        std::unique_ptr<donut::engine::BindingCache> m_bindingCache;
        std::array<nvrhi::TimerQueryHandle, kPostProcessTimerQueryCount> m_timerQueries{};
        std::array<uint8_t, kPostProcessTimerQueryCount> m_timerInFlight{};
        uint32_t m_timerIndex = 0;
        PostProcessPassHud m_hud;
    };
}
