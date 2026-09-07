#include "PostProcessPass.h"

#include <donut/core/log.h>
#include <nvrhi/utils.h>

#include <cmath>
#include <cstdlib>

using namespace donut;

namespace renderlab
{
    nvrhi::RasterState MakePostProcessRasterState()
    {
        nvrhi::RasterState raster;
        raster.setCullNone();
        raster.setDepthClipEnable(true);
        return raster;
    }

    nvrhi::DepthStencilState MakePostProcessDepthState()
    {
        nvrhi::DepthStencilState depth;
        depth.setDepthTestEnable(false);
        depth.setDepthWriteEnable(false);
        return depth;
    }

    PostProcessPassInputs MakePostProcessPassInputs(
        nvrhi::ITexture* hdrSceneColor,
        const TonemapConstants& tonemapConstants)
    {
        PostProcessPassInputs inputs;
        inputs.hdrSceneColor = hdrSceneColor;
        inputs.tonemapConstants = &tonemapConstants;
        return inputs;
    }

    bool ParseExposureEv(std::string_view text, float& exposureEV, std::string& error)
    {
        if (text.empty())
        {
            error = "--exposure-ev requires a finite float value.";
            return false;
        }

        const std::string valueText(text);
        char* end = nullptr;
        const float parsed = std::strtof(valueText.c_str(), &end);
        if (end == valueText.c_str() || *end != '\0')
        {
            error = "Unknown --exposure-ev '" + valueText + "'. Expected a finite float, e.g. -3, 0, 2.5.";
            return false;
        }
        if (!std::isfinite(parsed))
        {
            // No clamp on the EV range (docs/postprocess.md section 3); non-finite
            // values are malformed input, not extreme exposures.
            error = "--exposure-ev requires a finite float value; '" + valueText + "' is not finite.";
            return false;
        }

        exposureEV = parsed;
        return true;
    }

    bool PostProcessPass::Init(nvrhi::IDevice* device, engine::ShaderFactory& shaderFactory)
    {
        if (!device)
        {
            log::error("PostProcessPass requires an NVRHI device.");
            return false;
        }

        m_device = device;
        m_bindingCache = std::make_unique<engine::BindingCache>(device);

        m_vertexShader = shaderFactory.CreateShader(
            "renderlab/postprocess_vs.hlsl",
            "main",
            nullptr,
            nvrhi::ShaderType::Vertex);
        m_pixelShader = shaderFactory.CreateShader(
            "renderlab/postprocess_ps.hlsl",
            "main",
            nullptr,
            nvrhi::ShaderType::Pixel);
        if (!m_vertexShader || !m_pixelShader)
        {
            log::error(
                "Failed to load post-process shaders. Rebuild so renderlab_shaders emits "
                "postprocess_vs.bin and postprocess_ps.bin.");
            return false;
        }

        nvrhi::BindingLayoutDesc layoutDesc;
        layoutDesc.visibility = nvrhi::ShaderType::Pixel;
        layoutDesc.bindings = {
            nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
            nvrhi::BindingLayoutItem::Texture_SRV(0),
        };
        m_bindingLayout = device->createBindingLayout(layoutDesc);
        if (!m_bindingLayout)
        {
            log::error("Failed to create the post-process binding layout.");
            return false;
        }

        m_tonemapCB = device->createBuffer(nvrhi::utils::CreateVolatileConstantBufferDesc(
            sizeof(TonemapConstants), "PostProcessTonemapConstants", kPostProcessConstantBufferVersions));
        if (!m_tonemapCB)
        {
            log::error("Failed to create the post-process constant buffer.");
            return false;
        }

        for (uint32_t index = 0; index < kPostProcessTimerQueryCount; ++index)
        {
            m_timerQueries[index] = device->createTimerQuery();
            if (!m_timerQueries[index])
            {
                log::error("Failed to create post-process timer queries.");
                return false;
            }
        }

        log::info("PostProcessPass initialized (S3.2 exposure + UE Filmic tone map).");
        return true;
    }

    void PostProcessPass::ReleaseSizeDependentResources()
    {
        m_framebuffer = nullptr;
        m_framebufferColor = nullptr;
        m_pipeline = nullptr;
        m_hasPipelineInfo = false;
        m_dumpTarget = nullptr;
        if (m_bindingCache)
        {
            m_bindingCache->Clear();
        }
    }

    nvrhi::ITexture* PostProcessPass::GetOrCreateDumpTarget(uint32_t width, uint32_t height)
    {
        if (!m_device || width == 0 || height == 0)
        {
            return nullptr;
        }

        if (m_dumpTarget)
        {
            const nvrhi::TextureDesc& desc = m_dumpTarget->getDesc();
            if (desc.width == width && desc.height == height)
            {
                return m_dumpTarget;
            }
        }

        nvrhi::TextureDesc desc;
        desc.width = width;
        desc.height = height;
        desc.depth = 1;
        desc.arraySize = 1;
        desc.mipLevels = 1;
        desc.sampleCount = 1;
        desc.format = nvrhi::Format::SRGBA8_UNORM;
        desc.dimension = nvrhi::TextureDimension::Texture2D;
        desc.debugName = "PostProcessColor";
        desc.isRenderTarget = true;
        desc.isShaderResource = true;
        desc.useClearValue = true;
        desc.clearValue = nvrhi::Color(0.f);
        desc.initialState = nvrhi::ResourceStates::RenderTarget;
        desc.keepInitialState = true;
        m_dumpTarget = m_device->createTexture(desc);
        if (!m_dumpTarget)
        {
            log::error("Failed to create PostProcessColor (%u x %u).", width, height);
            return nullptr;
        }
        return m_dumpTarget;
    }

    bool PostProcessPass::EnsureFramebuffer(nvrhi::ITexture* finalColor)
    {
        if (m_framebuffer && m_framebufferColor == finalColor)
        {
            return true;
        }

        nvrhi::FramebufferDesc desc;
        desc.addColorAttachment(finalColor);
        m_framebuffer = m_device->createFramebuffer(desc);
        if (!m_framebuffer)
        {
            log::error("Failed to create the post-process framebuffer.");
            return false;
        }

        m_framebufferColor = finalColor;
        return true;
    }

    nvrhi::IBindingSet* PostProcessPass::GetOrCreateBindingSet(const PostProcessPassInputs& inputs)
    {
        nvrhi::BindingSetDesc desc;
        desc.bindings = {
            nvrhi::BindingSetItem::ConstantBuffer(0, m_tonemapCB),
            nvrhi::BindingSetItem::Texture_SRV(0, inputs.hdrSceneColor),
        };
        return m_bindingCache->GetOrCreateBindingSet(desc, m_bindingLayout);
    }

    nvrhi::IGraphicsPipeline* PostProcessPass::GetOrCreatePipeline(nvrhi::IFramebuffer* framebuffer)
    {
        const nvrhi::FramebufferInfo& info = framebuffer->getFramebufferInfo();
        if (m_pipeline && m_hasPipelineInfo && m_pipelineInfo == info)
        {
            return m_pipeline;
        }

        nvrhi::GraphicsPipelineDesc desc;
        desc.primType = nvrhi::PrimitiveType::TriangleList;
        desc.VS = m_vertexShader;
        desc.PS = m_pixelShader;
        desc.bindingLayouts = { m_bindingLayout };
        desc.renderState.rasterState = MakePostProcessRasterState();
        desc.renderState.depthStencilState = MakePostProcessDepthState();
        desc.renderState.blendState.disableAlphaToCoverage();

        m_pipeline = m_device->createGraphicsPipeline(desc, info);
        if (!m_pipeline)
        {
            log::error("Failed to create the post-process graphics pipeline.");
            m_hasPipelineInfo = false;
            return nullptr;
        }

        m_pipelineInfo = info;
        m_hasPipelineInfo = true;
        return m_pipeline;
    }

    void PostProcessPass::Execute(
        nvrhi::ICommandList* commandList,
        const PostProcessPassInputs& inputs,
        const PostProcessPassOutputs& outputs)
    {
        if (!commandList || !m_device)
        {
            return;
        }

        commandList->beginMarker("PostProcess"); // matches renderlab::markers::kPostProcess

        int timingIndex = -1;
        for (uint32_t offset = 0; offset < kPostProcessTimerQueryCount; ++offset)
        {
            const uint32_t index = (m_timerIndex + offset) % kPostProcessTimerQueryCount;
            if (m_timerInFlight[index])
            {
                if (!m_device->pollTimerQuery(m_timerQueries[index]))
                {
                    continue;
                }
                m_hud.gpuTimeMilliseconds = m_device->getTimerQueryTime(m_timerQueries[index]) * 1000.f;
                m_hud.timestampValid = true;
                m_timerInFlight[index] = 0;
            }
            timingIndex = static_cast<int>(index);
            break;
        }

        if (timingIndex >= 0)
        {
            commandList->beginTimerQuery(m_timerQueries[static_cast<size_t>(timingIndex)]);
        }

        struct PassScope
        {
            nvrhi::ICommandList* commandList = nullptr;
            nvrhi::ITimerQuery* query = nullptr;
            uint8_t* inFlight = nullptr;
            uint32_t* timerIndex = nullptr;
            uint32_t nextIndex = 0;

            ~PassScope()
            {
                if (query)
                {
                    commandList->endTimerQuery(query);
                    if (inFlight)
                    {
                        *inFlight = 1;
                    }
                }
                if (timerIndex)
                {
                    *timerIndex = nextIndex;
                }
                commandList->endMarker();
            }
        };

        PassScope scope;
        scope.commandList = commandList;
        if (timingIndex >= 0)
        {
            scope.query = m_timerQueries[static_cast<size_t>(timingIndex)];
            scope.inFlight = &m_timerInFlight[static_cast<size_t>(timingIndex)];
            scope.timerIndex = &m_timerIndex;
            scope.nextIndex =
                (static_cast<uint32_t>(timingIndex) + 1) % kPostProcessTimerQueryCount;
        }

        if (!outputs.finalColor || !inputs.hdrSceneColor || !inputs.tonemapConstants)
        {
            log::error("PostProcessPass inputs/outputs are incomplete; skipping tone map.");
            return;
        }

        if (!EnsureFramebuffer(outputs.finalColor))
        {
            return;
        }

        nvrhi::IGraphicsPipeline* pipeline = GetOrCreatePipeline(m_framebuffer);
        nvrhi::IBindingSet* bindings = GetOrCreateBindingSet(inputs);
        if (!pipeline || !bindings)
        {
            return;
        }

        commandList->writeBuffer(m_tonemapCB, inputs.tonemapConstants, sizeof(TonemapConstants));

        const nvrhi::TextureDesc& colorDesc = outputs.finalColor->getDesc();
        const nvrhi::Viewport viewport(
            0.f, float(colorDesc.width), 0.f, float(colorDesc.height), 0.f, 1.f);

        nvrhi::GraphicsState state;
        state.pipeline = pipeline;
        state.framebuffer = m_framebuffer;
        state.viewport.addViewportAndScissorRect(viewport);
        state.bindings = { bindings };
        commandList->setGraphicsState(state);

        nvrhi::DrawArguments args;
        args.vertexCount = 3;
        args.instanceCount = 1;
        commandList->draw(args);
    }
}
