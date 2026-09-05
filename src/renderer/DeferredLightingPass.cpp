#include "DeferredLightingPass.h"

#include <donut/core/log.h>
#include <nvrhi/utils.h>

using namespace donut;

namespace renderlab
{
    nvrhi::RasterState MakeDeferredLightingRasterState()
    {
        nvrhi::RasterState raster;
        raster.setCullNone();
        raster.setDepthClipEnable(true);
        return raster;
    }

    nvrhi::DepthStencilState MakeDeferredLightingDepthState()
    {
        nvrhi::DepthStencilState depth;
        depth.setDepthTestEnable(false);
        depth.setDepthWriteEnable(false);
        return depth;
    }

    DeferredLightingPassInputs MakeDeferredLightingPassInputs(
        const GBufferTargets& targets,
        const ViewConstants& viewConstants,
        const LightingConstants& lightingConstants)
    {
        DeferredLightingPassInputs inputs;
        inputs.gbufferA = targets.GetShaderResource(GBufferTarget::A);
        inputs.gbufferB = targets.GetShaderResource(GBufferTarget::B);
        inputs.gbufferC = targets.GetShaderResource(GBufferTarget::C);
        inputs.gbufferDepth = targets.GetShaderResource(GBufferTarget::Depth);
        inputs.viewConstants = &viewConstants;
        inputs.lightingConstants = &lightingConstants;
        return inputs;
    }

    DeferredLightingPassOutputs MakeDeferredLightingPassOutputs(const HDRSceneColorTarget& hdr)
    {
        DeferredLightingPassOutputs outputs;
        outputs.hdrSceneColor = hdr.GetTexture();
        return outputs;
    }

    bool DeferredLightingPass::Init(nvrhi::IDevice* device, engine::ShaderFactory& shaderFactory)
    {
        if (!device)
        {
            log::error("DeferredLightingPass requires an NVRHI device.");
            return false;
        }

        m_device = device;
        m_bindingCache = std::make_unique<engine::BindingCache>(device);

        m_vertexShader = shaderFactory.CreateShader(
            "renderlab/deferred_lighting_vs.hlsl",
            "main",
            nullptr,
            nvrhi::ShaderType::Vertex);
        m_pixelShader = shaderFactory.CreateShader(
            "renderlab/deferred_lighting_ps.hlsl",
            "main",
            nullptr,
            nvrhi::ShaderType::Pixel);
        if (!m_vertexShader || !m_pixelShader)
        {
            log::error(
                "Failed to load deferred lighting shaders. Rebuild so renderlab_shaders emits "
                "deferred_lighting_vs.bin and deferred_lighting_ps.bin.");
            return false;
        }

        nvrhi::BindingLayoutDesc layoutDesc;
        layoutDesc.visibility = nvrhi::ShaderType::Pixel;
        layoutDesc.bindings = {
            nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
            nvrhi::BindingLayoutItem::VolatileConstantBuffer(1),
            nvrhi::BindingLayoutItem::Texture_SRV(0),
            nvrhi::BindingLayoutItem::Texture_SRV(1),
            nvrhi::BindingLayoutItem::Texture_SRV(2),
            nvrhi::BindingLayoutItem::Texture_SRV(3),
        };
        m_bindingLayout = device->createBindingLayout(layoutDesc);
        if (!m_bindingLayout)
        {
            log::error("Failed to create the deferred lighting binding layout.");
            return false;
        }

        m_viewCB = device->createBuffer(nvrhi::utils::CreateVolatileConstantBufferDesc(
            sizeof(ViewConstants), "DeferredLightingViewConstants", kDeferredLightingConstantBufferVersions));
        m_lightingCB = device->createBuffer(nvrhi::utils::CreateVolatileConstantBufferDesc(
            sizeof(LightingConstants),
            "DeferredLightingConstants",
            kDeferredLightingConstantBufferVersions));
        if (!m_viewCB || !m_lightingCB)
        {
            log::error("Failed to create deferred lighting constant buffers.");
            return false;
        }

        for (uint32_t index = 0; index < kDeferredLightingTimerQueryCount; ++index)
        {
            m_timerQueries[index] = device->createTimerQuery();
            if (!m_timerQueries[index])
            {
                log::error("Failed to create deferred lighting timer queries.");
                return false;
            }
        }

        log::info("DeferredLightingPass initialized (S2.2 directional N·L diagnostic).");
        return true;
    }

    void DeferredLightingPass::ReleaseSizeDependentResources()
    {
        m_framebuffer = nullptr;
        m_framebufferColor = nullptr;
        m_pipeline = nullptr;
        m_hasPipelineInfo = false;
        if (m_bindingCache)
        {
            m_bindingCache->Clear();
        }
    }

    bool DeferredLightingPass::EnsureFramebuffer(nvrhi::ITexture* hdrSceneColor)
    {
        if (m_framebuffer && m_framebufferColor == hdrSceneColor)
        {
            return true;
        }

        nvrhi::FramebufferDesc desc;
        desc.addColorAttachment(hdrSceneColor);
        m_framebuffer = m_device->createFramebuffer(desc);
        if (!m_framebuffer)
        {
            log::error("Failed to create the deferred lighting framebuffer.");
            return false;
        }

        m_framebufferColor = hdrSceneColor;
        return true;
    }

    nvrhi::IBindingSet* DeferredLightingPass::GetOrCreateBindingSet(
        const DeferredLightingPassInputs& inputs)
    {
        nvrhi::BindingSetDesc desc;
        desc.bindings = {
            nvrhi::BindingSetItem::ConstantBuffer(0, m_viewCB),
            nvrhi::BindingSetItem::ConstantBuffer(1, m_lightingCB),
            nvrhi::BindingSetItem::Texture_SRV(0, inputs.gbufferA),
            nvrhi::BindingSetItem::Texture_SRV(1, inputs.gbufferB),
            nvrhi::BindingSetItem::Texture_SRV(2, inputs.gbufferC),
            nvrhi::BindingSetItem::Texture_SRV(3, inputs.gbufferDepth),
        };
        return m_bindingCache->GetOrCreateBindingSet(desc, m_bindingLayout);
    }

    nvrhi::IGraphicsPipeline* DeferredLightingPass::GetOrCreatePipeline(
        nvrhi::IFramebuffer* framebuffer)
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
        desc.renderState.rasterState = MakeDeferredLightingRasterState();
        desc.renderState.depthStencilState = MakeDeferredLightingDepthState();
        desc.renderState.blendState.disableAlphaToCoverage();

        m_pipeline = m_device->createGraphicsPipeline(desc, info);
        if (!m_pipeline)
        {
            log::error("Failed to create the deferred lighting graphics pipeline.");
            m_hasPipelineInfo = false;
            return nullptr;
        }

        m_pipelineInfo = info;
        m_hasPipelineInfo = true;
        return m_pipeline;
    }

    void DeferredLightingPass::Execute(
        nvrhi::ICommandList* commandList,
        const DeferredLightingPassInputs& inputs,
        const DeferredLightingPassOutputs& outputs)
    {
        if (!commandList || !m_device)
        {
            return;
        }

        commandList->beginMarker("DeferredLighting"); // matches renderlab::markers::kDeferredLighting

        int timingIndex = -1;
        for (uint32_t offset = 0; offset < kDeferredLightingTimerQueryCount; ++offset)
        {
            const uint32_t index = (m_timerIndex + offset) % kDeferredLightingTimerQueryCount;
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
                (static_cast<uint32_t>(timingIndex) + 1) % kDeferredLightingTimerQueryCount;
        }

        if (!outputs.hdrSceneColor || !inputs.gbufferA || !inputs.gbufferB || !inputs.gbufferC ||
            !inputs.gbufferDepth || !inputs.viewConstants || !inputs.lightingConstants)
        {
            log::error("DeferredLightingPass inputs/outputs are incomplete; skipping.");
            return;
        }

        commandList->clearTextureFloat(
            outputs.hdrSceneColor, nvrhi::AllSubresources, kHDRSceneColorClear);

        if (!EnsureFramebuffer(outputs.hdrSceneColor))
        {
            return;
        }

        nvrhi::IGraphicsPipeline* pipeline = GetOrCreatePipeline(m_framebuffer);
        nvrhi::IBindingSet* bindings = GetOrCreateBindingSet(inputs);
        if (!pipeline || !bindings)
        {
            return;
        }

        commandList->writeBuffer(m_viewCB, inputs.viewConstants, sizeof(ViewConstants));
        commandList->writeBuffer(m_lightingCB, inputs.lightingConstants, sizeof(LightingConstants));

        const nvrhi::TextureDesc& colorDesc = outputs.hdrSceneColor->getDesc();
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
