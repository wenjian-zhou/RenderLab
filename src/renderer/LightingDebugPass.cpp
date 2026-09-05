#include "LightingDebugPass.h"

#include <donut/core/log.h>
#include <nvrhi/utils.h>

#include <cctype>

using namespace donut;

namespace renderlab
{
    namespace
    {
        std::string ToLowerAscii(std::string_view text)
        {
            std::string lower(text);
            for (char& ch : lower)
            {
                ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            }
            return lower;
        }

        constexpr LightingDebugModeInfo kModeInfo[] = {
            {
                LightingDebugMode::WorldPosition,
                kLightingDebugWorldPositionCli,
                "World position",
                "Reconstructed world-space P from GBufferDepth + ViewConstants via "
                "ReconstructWorldPosition. Displayed as frac(abs(P)). "
                "Background (deviceDepth <= 0) is magenta (1,0,1) and is not reconstructed. "
                "This is visualization encoding, not HDRSceneColor.",
                "lighting-world-position.png",
            },
            {
                LightingDebugMode::NdotL,
                kLightingDebugNdotLCli,
                "N dot L",
                "saturate(dot(N, toLight)) grayscale using the same directional toLight as lighting. "
                "N comes from normalize(GBufferB.rgb). "
                "Background (deviceDepth <= 0) is black (0,0,0). "
                "This is visualization encoding, not the HDR lighting path.",
                "lighting-ndotl.png",
            },
            {
                LightingDebugMode::Lit,
                kLightingDebugLitCli,
                "Lit (Reinhard HDR)",
                "Reads HDRSceneColor and displays Reinhard tonemap hdr/(1+hdr). "
                "This is visualization encoding only; HDRSceneColor remains scene-referred. "
                "Background pixels show Reinhard of backgroundRadiance written by DeferredLighting.",
                "lighting-lit.png",
            },
        };

        static_assert(sizeof(kModeInfo) / sizeof(kModeInfo[0]) == LightingDebugMode_Count);
    }

    nvrhi::RasterState MakeLightingDebugRasterState()
    {
        nvrhi::RasterState raster;
        raster.setCullNone();
        raster.setDepthClipEnable(true);
        return raster;
    }

    nvrhi::DepthStencilState MakeLightingDebugDepthState()
    {
        nvrhi::DepthStencilState depth;
        depth.setDepthTestEnable(false);
        depth.setDepthWriteEnable(false);
        return depth;
    }

    LightingDebugPassInputs MakeLightingDebugPassInputs(
        const GBufferTargets& targets,
        nvrhi::ITexture* hdrSceneColor,
        const ViewConstants& viewConstants,
        const LightingConstants& lightingConstants,
        LightingDebugMode mode)
    {
        LightingDebugPassInputs inputs;
        inputs.gbufferA = targets.GetShaderResource(GBufferTarget::A);
        inputs.gbufferB = targets.GetShaderResource(GBufferTarget::B);
        inputs.gbufferC = targets.GetShaderResource(GBufferTarget::C);
        inputs.gbufferDepth = targets.GetShaderResource(GBufferTarget::Depth);
        inputs.hdrSceneColor = hdrSceneColor;
        inputs.viewConstants = &viewConstants;
        inputs.lightingConstants = &lightingConstants;
        inputs.mode = mode;
        return inputs;
    }

    const LightingDebugModeInfo& GetLightingDebugModeInfo(LightingDebugMode mode)
    {
        const uint32_t index = static_cast<uint32_t>(mode);
        if (index >= LightingDebugMode_Count)
        {
            return kModeInfo[static_cast<size_t>(LightingDebugMode::Lit)];
        }
        return kModeInfo[index];
    }

    bool ParseLightingDebugMode(std::string_view text, LightingDebugMode& mode, std::string& error)
    {
        const std::string lower = ToLowerAscii(text);
        if (lower == kLightingDebugWorldPositionCli || lower == "worldposition" || lower == "position")
        {
            mode = LightingDebugMode::WorldPosition;
            return true;
        }
        if (lower == kLightingDebugNdotLCli || lower == "n-dot-l" || lower == "n.l")
        {
            mode = LightingDebugMode::NdotL;
            return true;
        }
        if (lower == kLightingDebugLitCli || lower == "reinhard" || lower == "hdr")
        {
            mode = LightingDebugMode::Lit;
            return true;
        }

        error =
            "Unknown --lighting-view '" + std::string(text) +
            "'. Expected world-position, ndotl, or lit.";
        return false;
    }

    bool LightingDebugPass::Init(nvrhi::IDevice* device, engine::ShaderFactory& shaderFactory)
    {
        if (!device)
        {
            log::error("LightingDebugPass requires an NVRHI device.");
            return false;
        }

        m_device = device;
        m_bindingCache = std::make_unique<engine::BindingCache>(device);

        m_vertexShader = shaderFactory.CreateShader(
            "renderlab/lighting_debug_vs.hlsl",
            "main",
            nullptr,
            nvrhi::ShaderType::Vertex);
        m_pixelShader = shaderFactory.CreateShader(
            "renderlab/lighting_debug_ps.hlsl",
            "main",
            nullptr,
            nvrhi::ShaderType::Pixel);
        if (!m_vertexShader || !m_pixelShader)
        {
            log::error(
                "Failed to load lighting debug shaders. Rebuild so renderlab_shaders emits "
                "lighting_debug_vs.bin and lighting_debug_ps.bin.");
            return false;
        }

        nvrhi::BindingLayoutDesc layoutDesc;
        layoutDesc.visibility = nvrhi::ShaderType::Pixel;
        layoutDesc.bindings = {
            nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
            nvrhi::BindingLayoutItem::VolatileConstantBuffer(1),
            nvrhi::BindingLayoutItem::VolatileConstantBuffer(2),
            nvrhi::BindingLayoutItem::Texture_SRV(0),
            nvrhi::BindingLayoutItem::Texture_SRV(1),
            nvrhi::BindingLayoutItem::Texture_SRV(2),
            nvrhi::BindingLayoutItem::Texture_SRV(3),
            nvrhi::BindingLayoutItem::Texture_SRV(4),
        };
        m_bindingLayout = device->createBindingLayout(layoutDesc);
        if (!m_bindingLayout)
        {
            log::error("Failed to create the lighting debug binding layout.");
            return false;
        }

        m_viewCB = device->createBuffer(nvrhi::utils::CreateVolatileConstantBufferDesc(
            sizeof(ViewConstants), "LightingDebugViewConstants", kLightingDebugConstantBufferVersions));
        m_lightingCB = device->createBuffer(nvrhi::utils::CreateVolatileConstantBufferDesc(
            sizeof(LightingConstants),
            "LightingDebugLightingConstants",
            kLightingDebugConstantBufferVersions));
        m_debugCB = device->createBuffer(nvrhi::utils::CreateVolatileConstantBufferDesc(
            sizeof(LightingDebugConstants),
            "LightingDebugConstants",
            kLightingDebugConstantBufferVersions));
        if (!m_viewCB || !m_lightingCB || !m_debugCB)
        {
            log::error("Failed to create lighting debug constant buffers.");
            return false;
        }

        log::info("LightingDebugPass initialized (world-position / N·L / lit Reinhard).");
        return true;
    }

    void LightingDebugPass::ReleaseSizeDependentResources()
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

    nvrhi::ITexture* LightingDebugPass::GetOrCreateDumpTarget(uint32_t width, uint32_t height)
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
        desc.debugName = "LightingDebugColor";
        desc.isRenderTarget = true;
        desc.isShaderResource = true;
        desc.useClearValue = true;
        desc.clearValue = nvrhi::Color(0.f);
        desc.initialState = nvrhi::ResourceStates::RenderTarget;
        desc.keepInitialState = true;
        m_dumpTarget = m_device->createTexture(desc);
        if (!m_dumpTarget)
        {
            log::error("Failed to create LightingDebugColor (%u x %u).", width, height);
            return nullptr;
        }
        return m_dumpTarget;
    }

    bool LightingDebugPass::EnsureFramebuffer(nvrhi::ITexture* debugColor)
    {
        if (m_framebuffer && m_framebufferColor == debugColor)
        {
            return true;
        }

        nvrhi::FramebufferDesc desc;
        desc.addColorAttachment(debugColor);
        m_framebuffer = m_device->createFramebuffer(desc);
        if (!m_framebuffer)
        {
            log::error("Failed to create the lighting debug framebuffer.");
            return false;
        }

        m_framebufferColor = debugColor;
        return true;
    }

    nvrhi::IBindingSet* LightingDebugPass::GetOrCreateBindingSet(const LightingDebugPassInputs& inputs)
    {
        nvrhi::BindingSetDesc desc;
        desc.bindings = {
            nvrhi::BindingSetItem::ConstantBuffer(0, m_viewCB),
            nvrhi::BindingSetItem::ConstantBuffer(1, m_lightingCB),
            nvrhi::BindingSetItem::ConstantBuffer(2, m_debugCB),
            nvrhi::BindingSetItem::Texture_SRV(0, inputs.gbufferA),
            nvrhi::BindingSetItem::Texture_SRV(1, inputs.gbufferB),
            nvrhi::BindingSetItem::Texture_SRV(2, inputs.gbufferC),
            nvrhi::BindingSetItem::Texture_SRV(3, inputs.gbufferDepth),
            nvrhi::BindingSetItem::Texture_SRV(4, inputs.hdrSceneColor),
        };
        return m_bindingCache->GetOrCreateBindingSet(desc, m_bindingLayout);
    }

    nvrhi::IGraphicsPipeline* LightingDebugPass::GetOrCreatePipeline(nvrhi::IFramebuffer* framebuffer)
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
        desc.renderState.rasterState = MakeLightingDebugRasterState();
        desc.renderState.depthStencilState = MakeLightingDebugDepthState();
        desc.renderState.blendState.disableAlphaToCoverage();

        m_pipeline = m_device->createGraphicsPipeline(desc, info);
        if (!m_pipeline)
        {
            log::error("Failed to create the lighting debug graphics pipeline.");
            m_hasPipelineInfo = false;
            return nullptr;
        }

        m_pipelineInfo = info;
        m_hasPipelineInfo = true;
        return m_pipeline;
    }

    void LightingDebugPass::Execute(
        nvrhi::ICommandList* commandList,
        const LightingDebugPassInputs& inputs,
        const LightingDebugPassOutputs& outputs)
    {
        if (!commandList || !m_device)
        {
            return;
        }

        commandList->beginMarker("LightingDebug"); // matches renderlab::markers::kLightingDebug

        struct PassScope
        {
            nvrhi::ICommandList* commandList = nullptr;
            ~PassScope()
            {
                if (commandList)
                {
                    commandList->endMarker();
                }
            }
        } scope{ commandList };

        if (!outputs.debugColor || !inputs.gbufferA || !inputs.gbufferB || !inputs.gbufferC ||
            !inputs.gbufferDepth || !inputs.hdrSceneColor || !inputs.viewConstants ||
            !inputs.lightingConstants)
        {
            log::error("LightingDebugPass inputs/outputs are incomplete; skipping visualization.");
            return;
        }

        if (!EnsureFramebuffer(outputs.debugColor))
        {
            return;
        }

        nvrhi::IGraphicsPipeline* pipeline = GetOrCreatePipeline(m_framebuffer);
        nvrhi::IBindingSet* bindings = GetOrCreateBindingSet(inputs);
        if (!pipeline || !bindings)
        {
            return;
        }

        LightingDebugConstants debugConstants = {};
        debugConstants.mode = static_cast<uint>(inputs.mode);
        commandList->writeBuffer(m_viewCB, inputs.viewConstants, sizeof(ViewConstants));
        commandList->writeBuffer(m_lightingCB, inputs.lightingConstants, sizeof(LightingConstants));
        commandList->writeBuffer(m_debugCB, &debugConstants, sizeof(debugConstants));

        const nvrhi::TextureDesc& colorDesc = outputs.debugColor->getDesc();
        const nvrhi::Viewport viewport(0.f, float(colorDesc.width), 0.f, float(colorDesc.height), 0.f, 1.f);

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
