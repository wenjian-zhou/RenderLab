#include "GBufferDebugPass.h"

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

        constexpr GBufferDebugModeInfo kModeInfo[] = {
            {
                GBufferDebugMode::BaseColor,
                "base-color",
                "Base color",
                "GBufferA.rgb via DecodeGBuffer: linear Rec.709 reflectance. "
                "GBufferA is an sRGB target, so the SRV returns linear. "
                "Displayed with the back-buffer hardware sRGB OETF. "
                "This is visualization encoding, not the lighting path and not the S3 tone map. "
                "Background clear is (0,0,0).",
                "gbuffer-base-color.png",
            },
            {
                GBufferDebugMode::WorldNormal,
                "world-normal",
                "World normal",
                "GBufferB.rgb via DecodeGBuffer: raw float16 world-space unit shading normal in [-1, 1]. "
                "Display remap is 0.5 * n + 0.5 (not stored that way). "
                "Background (deviceDepth == 0) stays black; the zero vector is not remapped to gray.",
                "gbuffer-world-normal.png",
            },
            {
                GBufferDebugMode::Roughness,
                "roughness",
                "Roughness",
                "GBufferB.a via DecodeGBuffer: perceptual glTF roughness in [0, 1], not GGX alpha. "
                "Shown as linear grayscale written to the sRGB back buffer. "
                "Background clear is 0.",
                "gbuffer-roughness.png",
            },
            {
                GBufferDebugMode::Metallic,
                "metallic",
                "Metallic",
                "GBufferC.r via DecodeGBuffer: glTF metallic in [0, 1]. "
                "Shown as linear grayscale written to the sRGB back buffer. "
                "Background clear is 0.",
                "gbuffer-metallic.png",
            },
            {
                GBufferDebugMode::AoFlags,
                "ao-flags",
                "AO / material flags",
                "DecodeGBuffer: R = AO (GBufferC.g), G = ShadingValid (bit 0), B = TwoSided (bit 1). "
                "Background is (0,0,0) with ShadingValid off. Flags are unpacked with UnpackGBufferFlags.",
                "gbuffer-ao-flags.png",
            },
            {
                GBufferDebugMode::LinearDepth,
                "linear-depth",
                "Linearized depth",
                "GBufferDepth R32_FLOAT SRV (existing S1.3 texture). "
                "viewZ = zNear / deviceDepth, rejected when deviceDepth == 0. "
                "Displayed as viewZ / (viewZ + 1), which is finite and monotonic. "
                "Background / cleared depth is magenta (1,0,1) and is not reconstructed.",
                "gbuffer-linear-depth.png",
            },
        };

        static_assert(sizeof(kModeInfo) / sizeof(kModeInfo[0]) == GBufferDebugMode_Count);
    }

    nvrhi::RasterState MakeGBufferDebugRasterState()
    {
        nvrhi::RasterState raster;
        raster.setCullNone();
        raster.setDepthClipEnable(true);
        return raster;
    }

    nvrhi::DepthStencilState MakeGBufferDebugDepthState()
    {
        nvrhi::DepthStencilState depth;
        depth.setDepthTestEnable(false);
        depth.setDepthWriteEnable(false);
        return depth;
    }

    GBufferDebugPassInputs MakeGBufferDebugPassInputs(
        const GBufferTargets& targets,
        const ViewConstants& viewConstants,
        GBufferDebugMode mode)
    {
        GBufferDebugPassInputs inputs;
        inputs.gbufferA = targets.GetShaderResource(GBufferTarget::A);
        inputs.gbufferB = targets.GetShaderResource(GBufferTarget::B);
        inputs.gbufferC = targets.GetShaderResource(GBufferTarget::C);
        inputs.gbufferDepth = targets.GetShaderResource(GBufferTarget::Depth);
        inputs.viewConstants = &viewConstants;
        inputs.mode = mode;
        return inputs;
    }

    const GBufferDebugModeInfo& GetGBufferDebugModeInfo(GBufferDebugMode mode)
    {
        const uint32_t index = static_cast<uint32_t>(mode);
        if (index >= GBufferDebugMode_Count)
        {
            return kModeInfo[0];
        }
        return kModeInfo[index];
    }

    bool ParseGBufferDebugMode(std::string_view text, GBufferDebugMode& mode, std::string& error)
    {
        const std::string lower = ToLowerAscii(text);
        if (lower == "base-color" || lower == "basecolor" || lower == "albedo")
        {
            mode = GBufferDebugMode::BaseColor;
            return true;
        }
        if (lower == "world-normal" || lower == "normal" || lower == "normals")
        {
            mode = GBufferDebugMode::WorldNormal;
            return true;
        }
        if (lower == "roughness")
        {
            mode = GBufferDebugMode::Roughness;
            return true;
        }
        if (lower == "metallic" || lower == "metalness")
        {
            mode = GBufferDebugMode::Metallic;
            return true;
        }
        if (lower == "ao-flags" || lower == "ao" || lower == "flags")
        {
            mode = GBufferDebugMode::AoFlags;
            return true;
        }
        if (lower == "linear-depth" || lower == "depth" || lower == "linearized-depth")
        {
            mode = GBufferDebugMode::LinearDepth;
            return true;
        }

        error =
            "Unknown --gbuffer-view '" + std::string(text) +
            "'. Expected base-color, world-normal, roughness, metallic, ao-flags, or linear-depth.";
        return false;
    }

    bool LinearizeViewDepth(float deviceDepth, float zNear, float& viewZ)
    {
        if (deviceDepth <= 0.f)
        {
            return false;
        }
        viewZ = zNear / deviceDepth;
        return true;
    }

    float LinearDepthDisplayValue(float viewZ)
    {
        return viewZ / (viewZ + 1.f);
    }

    bool GBufferDebugPass::Init(nvrhi::IDevice* device, engine::ShaderFactory& shaderFactory)
    {
        if (!device)
        {
            log::error("GBufferDebugPass requires an NVRHI device.");
            return false;
        }

        m_device = device;
        m_bindingCache = std::make_unique<engine::BindingCache>(device);

        m_vertexShader = shaderFactory.CreateShader(
            "renderlab/gbuffer_debug_vs.hlsl",
            "main",
            nullptr,
            nvrhi::ShaderType::Vertex);
        m_pixelShader = shaderFactory.CreateShader(
            "renderlab/gbuffer_debug_ps.hlsl",
            "main",
            nullptr,
            nvrhi::ShaderType::Pixel);
        if (!m_vertexShader || !m_pixelShader)
        {
            log::error(
                "Failed to load GBuffer debug shaders. Rebuild so renderlab_shaders emits "
                "gbuffer_debug_vs.bin and gbuffer_debug_ps.bin.");
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
            log::error("Failed to create the GBuffer debug binding layout.");
            return false;
        }

        m_viewCB = device->createBuffer(nvrhi::utils::CreateVolatileConstantBufferDesc(
            sizeof(ViewConstants), "GBufferDebugViewConstants", kGBufferDebugConstantBufferVersions));
        m_debugCB = device->createBuffer(nvrhi::utils::CreateVolatileConstantBufferDesc(
            sizeof(GBufferDebugConstants), "GBufferDebugConstants", kGBufferDebugConstantBufferVersions));
        if (!m_viewCB || !m_debugCB)
        {
            log::error("Failed to create GBuffer debug constant buffers.");
            return false;
        }

        log::info("GBufferDebugPass initialized (fullscreen DecodeGBuffer visualization).");
        return true;
    }

    void GBufferDebugPass::ReleaseSizeDependentResources()
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

    nvrhi::ITexture* GBufferDebugPass::GetOrCreateDumpTarget(uint32_t width, uint32_t height)
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
        desc.debugName = "GBufferDebugColor";
        desc.isRenderTarget = true;
        desc.isShaderResource = true;
        desc.useClearValue = true;
        desc.clearValue = nvrhi::Color(0.f);
        desc.initialState = nvrhi::ResourceStates::RenderTarget;
        desc.keepInitialState = true;
        m_dumpTarget = m_device->createTexture(desc);
        if (!m_dumpTarget)
        {
            log::error("Failed to create GBufferDebugColor (%u x %u).", width, height);
            return nullptr;
        }
        return m_dumpTarget;
    }

    bool GBufferDebugPass::EnsureFramebuffer(nvrhi::ITexture* debugColor)
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
            log::error("Failed to create the GBuffer debug framebuffer.");
            return false;
        }

        m_framebufferColor = debugColor;
        return true;
    }

    nvrhi::IBindingSet* GBufferDebugPass::GetOrCreateBindingSet(const GBufferDebugPassInputs& inputs)
    {
        nvrhi::BindingSetDesc desc;
        desc.bindings = {
            nvrhi::BindingSetItem::ConstantBuffer(0, m_viewCB),
            nvrhi::BindingSetItem::ConstantBuffer(1, m_debugCB),
            nvrhi::BindingSetItem::Texture_SRV(0, inputs.gbufferA),
            nvrhi::BindingSetItem::Texture_SRV(1, inputs.gbufferB),
            nvrhi::BindingSetItem::Texture_SRV(2, inputs.gbufferC),
            nvrhi::BindingSetItem::Texture_SRV(3, inputs.gbufferDepth),
        };
        return m_bindingCache->GetOrCreateBindingSet(desc, m_bindingLayout);
    }

    nvrhi::IGraphicsPipeline* GBufferDebugPass::GetOrCreatePipeline(nvrhi::IFramebuffer* framebuffer)
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
        desc.renderState.rasterState = MakeGBufferDebugRasterState();
        desc.renderState.depthStencilState = MakeGBufferDebugDepthState();
        desc.renderState.blendState.disableAlphaToCoverage();

        m_pipeline = m_device->createGraphicsPipeline(desc, info);
        if (!m_pipeline)
        {
            log::error("Failed to create the GBuffer debug graphics pipeline.");
            m_hasPipelineInfo = false;
            return nullptr;
        }

        m_pipelineInfo = info;
        m_hasPipelineInfo = true;
        return m_pipeline;
    }

    void GBufferDebugPass::Execute(
        nvrhi::ICommandList* commandList,
        const GBufferDebugPassInputs& inputs,
        const GBufferDebugPassOutputs& outputs)
    {
        if (!commandList || !m_device)
        {
            return;
        }

        commandList->beginMarker("GBufferDebug");

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
            !inputs.gbufferDepth)
        {
            log::error("GBufferDebugPass inputs/outputs are incomplete; skipping visualization.");
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

        ViewConstants view = inputs.viewConstants ? *inputs.viewConstants : ViewConstants{};
        GBufferDebugConstants debugConstants = {};
        debugConstants.mode = static_cast<uint>(inputs.mode);
        commandList->writeBuffer(m_viewCB, &view, sizeof(view));
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
