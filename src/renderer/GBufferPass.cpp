#include "GBufferPass.h"

#include "GBufferContract.h"

#include <donut/core/log.h>
#include <donut/core/math/math.h>
#include <nvrhi/utils.h>

#include <algorithm>
#include <vector>

using namespace donut;
using namespace donut::math;

namespace renderlab
{
    namespace
    {
        constexpr uint32_t kPipelineCount = 4;

        uint32_t PackUnorm8(float r, float g, float b, float a)
        {
            const auto toByte = [](float value) -> uint32_t {
                const float clamped = std::clamp(value, 0.f, 1.f);
                return static_cast<uint32_t>(clamped * 255.f + 0.5f);
            };
            return toByte(r) | (toByte(g) << 8) | (toByte(b) << 16) | (toByte(a) << 24);
        }

        nvrhi::TextureHandle CreateFallbackTexture(
            nvrhi::IDevice* device,
            nvrhi::ICommandList* commandList,
            const char* debugName,
            nvrhi::Format format,
            uint32_t packedRgba)
        {
            nvrhi::TextureDesc desc;
            desc.width = 1;
            desc.height = 1;
            desc.depth = 1;
            desc.arraySize = 1;
            desc.mipLevels = 1;
            desc.sampleCount = 1;
            desc.format = format;
            desc.dimension = nvrhi::TextureDimension::Texture2D;
            desc.debugName = debugName;
            desc.isShaderResource = true;
            desc.initialState = nvrhi::ResourceStates::Common;
            desc.keepInitialState = false;

            nvrhi::TextureHandle texture = device->createTexture(desc);
            if (!texture)
            {
                return nullptr;
            }

            commandList->beginTrackingTextureState(texture, nvrhi::AllSubresources, nvrhi::ResourceStates::Common);
            commandList->writeTexture(texture, 0, 0, &packedRgba, 0);
            commandList->setPermanentTextureState(texture, nvrhi::ResourceStates::ShaderResource);
            return texture;
        }
    }

    nvrhi::RasterState MakeGBufferRasterState(const GBufferRasterKey& key)
    {
        nvrhi::RasterState raster;
        raster.setFrontCounterClockwise(key.frontCounterClockwise);
        raster.setCullMode(key.twoSided ? nvrhi::RasterCullMode::None : nvrhi::RasterCullMode::Back);
        raster.setDepthClipEnable(true);
        return raster;
    }

    nvrhi::DepthStencilState MakeGBufferDepthState()
    {
        nvrhi::DepthStencilState depth;
        depth.setDepthTestEnable(true);
        depth.setDepthWriteEnable(true);
        depth.setDepthFunc(nvrhi::ComparisonFunc::GreaterOrEqual);
        return depth;
    }

    GBufferPassOutputs MakeGBufferPassOutputs(const GBufferTargets& targets)
    {
        GBufferPassOutputs outputs;
        outputs.gbufferA = targets.GetTexture(GBufferTarget::A);
        outputs.gbufferB = targets.GetTexture(GBufferTarget::B);
        outputs.gbufferC = targets.GetTexture(GBufferTarget::C);
        outputs.gbufferDepth = targets.GetTexture(GBufferTarget::Depth);
        return outputs;
    }

    bool GBufferPass::Init(nvrhi::IDevice* device, engine::ShaderFactory& shaderFactory)
    {
        if (!device)
        {
            log::error("GBufferPass requires an NVRHI device.");
            return false;
        }

        m_device = device;
        m_bindingCache = std::make_unique<engine::BindingCache>(device);

        m_vertexShader = shaderFactory.CreateShader(
            "renderlab/gbuffer_vs.hlsl",
            "main",
            nullptr,
            nvrhi::ShaderType::Vertex);
        m_pixelShader = shaderFactory.CreateShader(
            "renderlab/gbuffer_ps.hlsl",
            "main",
            nullptr,
            nvrhi::ShaderType::Pixel);
        if (!m_vertexShader || !m_pixelShader)
        {
            log::error(
                "Failed to load GBuffer shaders. Rebuild so renderlab_shaders emits "
                "gbuffer_vs.bin and gbuffer_ps.bin next to the Donut dxil shaders.");
            return false;
        }

        const nvrhi::VertexAttributeDesc inputDescs[] = {
            nvrhi::VertexAttributeDesc()
                .setName("POSITION")
                .setFormat(nvrhi::Format::RGB32_FLOAT)
                .setBufferIndex(0)
                .setElementStride(sizeof(float3)),
            nvrhi::VertexAttributeDesc()
                .setName("TEXCOORD")
                .setFormat(nvrhi::Format::RG32_FLOAT)
                .setBufferIndex(1)
                .setElementStride(sizeof(float2)),
            nvrhi::VertexAttributeDesc()
                .setName("NORMAL")
                .setFormat(nvrhi::Format::RGBA8_SNORM)
                .setBufferIndex(2)
                .setElementStride(sizeof(uint32_t)),
            nvrhi::VertexAttributeDesc()
                .setName("TANGENT")
                .setFormat(nvrhi::Format::RGBA8_SNORM)
                .setBufferIndex(3)
                .setElementStride(sizeof(uint32_t)),
        };
        m_inputLayout = device->createInputLayout(inputDescs, 4, m_vertexShader);
        if (!m_inputLayout)
        {
            log::error("Failed to create the GBuffer input layout.");
            return false;
        }

        nvrhi::BindingLayoutDesc layoutDesc;
        layoutDesc.visibility = nvrhi::ShaderType::Vertex | nvrhi::ShaderType::Pixel;
        layoutDesc.bindings = {
            nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
            nvrhi::BindingLayoutItem::VolatileConstantBuffer(1),
            nvrhi::BindingLayoutItem::VolatileConstantBuffer(2),
            nvrhi::BindingLayoutItem::VolatileConstantBuffer(3),
            nvrhi::BindingLayoutItem::Texture_SRV(0),
            nvrhi::BindingLayoutItem::Texture_SRV(1),
            nvrhi::BindingLayoutItem::Texture_SRV(2),
            nvrhi::BindingLayoutItem::Texture_SRV(3),
            nvrhi::BindingLayoutItem::Sampler(0),
        };
        m_bindingLayout = device->createBindingLayout(layoutDesc);
        if (!m_bindingLayout)
        {
            log::error("Failed to create the GBuffer binding layout.");
            return false;
        }

        m_frameCB = device->createBuffer(nvrhi::utils::CreateVolatileConstantBufferDesc(
            sizeof(FrameConstants), "GBufferFrameConstants", kGBufferConstantBufferVersions));
        m_viewCB = device->createBuffer(nvrhi::utils::CreateVolatileConstantBufferDesc(
            sizeof(ViewConstants), "GBufferViewConstants", kGBufferConstantBufferVersions));
        m_instanceCB = device->createBuffer(nvrhi::utils::CreateVolatileConstantBufferDesc(
            sizeof(InstanceConstants), "GBufferInstanceConstants", kGBufferConstantBufferVersions));
        m_materialCB = device->createBuffer(nvrhi::utils::CreateVolatileConstantBufferDesc(
            sizeof(MaterialParams), "GBufferMaterialParams", kGBufferConstantBufferVersions));
        if (!m_frameCB || !m_viewCB || !m_instanceCB || !m_materialCB)
        {
            log::error("Failed to create GBuffer constant buffers.");
            return false;
        }

        nvrhi::SamplerDesc samplerDesc;
        samplerDesc.setAllFilters(true);
        samplerDesc.setAllAddressModes(nvrhi::SamplerAddressMode::Wrap);
        m_sampler = device->createSampler(samplerDesc);
        if (!m_sampler)
        {
            log::error("Failed to create the GBuffer material sampler.");
            return false;
        }

        if (!CreateFallbackTextures())
        {
            return false;
        }

        for (uint32_t index = 0; index < kGBufferTimerQueryCount; ++index)
        {
            m_timerQueries[index] = device->createTimerQuery();
            if (!m_timerQueries[index])
            {
                log::error("Failed to create GBuffer timer queries.");
                return false;
            }
        }

        log::info("GBufferPass initialized (opaque MRT + reversed-Z depth, linear wrap sampler).");
        return true;
    }

    bool GBufferPass::CreateFallbackTextures()
    {
        nvrhi::CommandListHandle commandList = m_device->createCommandList();
        if (!commandList)
        {
            log::error("Failed to create a command list for GBuffer fallback textures.");
            return false;
        }

        commandList->open();
        m_fallbackWhiteSrgb = CreateFallbackTexture(
            m_device,
            commandList,
            "GBufferFallbackWhiteSrgb",
            nvrhi::Format::SRGBA8_UNORM,
            PackUnorm8(kFallbackBaseColorRgba.x, kFallbackBaseColorRgba.y, kFallbackBaseColorRgba.z, kFallbackBaseColorRgba.w));
        m_fallbackWhite = CreateFallbackTexture(
            m_device,
            commandList,
            "GBufferFallbackWhite",
            nvrhi::Format::RGBA8_UNORM,
            PackUnorm8(kFallbackMetalRoughRgba.x, kFallbackMetalRoughRgba.y, kFallbackMetalRoughRgba.z, kFallbackMetalRoughRgba.w));
        m_fallbackFlatNormal = CreateFallbackTexture(
            m_device,
            commandList,
            "GBufferFallbackFlatNormal",
            nvrhi::Format::RGBA8_UNORM,
            PackUnorm8(kFallbackNormalRgba.x, kFallbackNormalRgba.y, kFallbackNormalRgba.z, kFallbackNormalRgba.w));
        m_fallbackOcclusion = CreateFallbackTexture(
            m_device,
            commandList,
            "GBufferFallbackOcclusion",
            nvrhi::Format::RGBA8_UNORM,
            PackUnorm8(kFallbackOcclusionRgba.x, kFallbackOcclusionRgba.y, kFallbackOcclusionRgba.z, kFallbackOcclusionRgba.w));
        commandList->commitBarriers();
        commandList->close();
        m_device->executeCommandList(commandList);

        if (!m_fallbackWhiteSrgb || !m_fallbackWhite || !m_fallbackFlatNormal || !m_fallbackOcclusion)
        {
            log::error("Failed to create GBuffer 1x1 fallback textures.");
            return false;
        }
        return true;
    }

    void GBufferPass::ReleaseSizeDependentResources()
    {
        m_framebuffer = nullptr;
        m_framebufferA = nullptr;
        m_framebufferB = nullptr;
        m_framebufferC = nullptr;
        m_framebufferDepth = nullptr;
    }

    bool GBufferPass::EnsureDummyVertexCapacity(nvrhi::ICommandList* commandList, uint32_t vertexCount)
    {
        const uint32_t required = std::max(vertexCount, kGBufferDummyVertexMinimum);
        if (m_dummyTexCoord && m_dummyTangent && m_dummyVertexCount >= required)
        {
            return true;
        }

        nvrhi::BufferDesc texCoordDesc;
        texCoordDesc.byteSize = static_cast<uint64_t>(required) * sizeof(float2);
        texCoordDesc.isVertexBuffer = true;
        texCoordDesc.debugName = "GBufferDummyTexCoord";
        texCoordDesc.initialState = nvrhi::ResourceStates::VertexBuffer;
        texCoordDesc.keepInitialState = true;
        m_dummyTexCoord = m_device->createBuffer(texCoordDesc);

        nvrhi::BufferDesc tangentDesc;
        tangentDesc.byteSize = static_cast<uint64_t>(required) * sizeof(uint32_t);
        tangentDesc.isVertexBuffer = true;
        tangentDesc.debugName = "GBufferDummyTangent";
        tangentDesc.initialState = nvrhi::ResourceStates::VertexBuffer;
        tangentDesc.keepInitialState = true;
        m_dummyTangent = m_device->createBuffer(tangentDesc);

        if (!m_dummyTexCoord || !m_dummyTangent)
        {
            log::error("Failed to create GBuffer dummy vertex streams.");
            return false;
        }

        std::vector<float2> zeros(required, float2(0.f));
        const uint32_t packedTangent = vectorToSnorm8(float4(1.f, 0.f, 0.f, 1.f));
        std::vector<uint32_t> tangents(required, packedTangent);
        commandList->writeBuffer(m_dummyTexCoord, zeros.data(), zeros.size() * sizeof(float2));
        commandList->writeBuffer(m_dummyTangent, tangents.data(), tangents.size() * sizeof(uint32_t));
        m_dummyVertexCount = required;
        return true;
    }

    bool GBufferPass::EnsureFramebuffer(const GBufferPassOutputs& outputs)
    {
        if (m_framebuffer &&
            m_framebufferA == outputs.gbufferA &&
            m_framebufferB == outputs.gbufferB &&
            m_framebufferC == outputs.gbufferC &&
            m_framebufferDepth == outputs.gbufferDepth)
        {
            return true;
        }

        nvrhi::FramebufferDesc desc;
        desc.addColorAttachment(outputs.gbufferA);
        desc.addColorAttachment(outputs.gbufferB);
        desc.addColorAttachment(outputs.gbufferC);
        desc.setDepthAttachment(outputs.gbufferDepth);
        m_framebuffer = m_device->createFramebuffer(desc);
        if (!m_framebuffer)
        {
            log::error("Failed to create the GBuffer framebuffer.");
            return false;
        }

        m_framebufferA = outputs.gbufferA;
        m_framebufferB = outputs.gbufferB;
        m_framebufferC = outputs.gbufferC;
        m_framebufferDepth = outputs.gbufferDepth;
        return true;
    }

    nvrhi::IBindingSet* GBufferPass::GetOrCreateBindingSet(const DrawRecord& draw)
    {
        nvrhi::ITexture* baseColor = draw.baseColor.texture ? draw.baseColor.texture.Get() : m_fallbackWhiteSrgb.Get();
        nvrhi::ITexture* metalRough = draw.metalRough.texture ? draw.metalRough.texture.Get() : m_fallbackWhite.Get();
        nvrhi::ITexture* normal = draw.normal.texture ? draw.normal.texture.Get() : m_fallbackFlatNormal.Get();
        nvrhi::ITexture* occlusion = draw.occlusion.texture ? draw.occlusion.texture.Get() : m_fallbackOcclusion.Get();

        nvrhi::BindingSetDesc desc;
        desc.bindings = {
            nvrhi::BindingSetItem::ConstantBuffer(0, m_frameCB),
            nvrhi::BindingSetItem::ConstantBuffer(1, m_viewCB),
            nvrhi::BindingSetItem::ConstantBuffer(2, m_instanceCB),
            nvrhi::BindingSetItem::ConstantBuffer(3, m_materialCB),
            nvrhi::BindingSetItem::Texture_SRV(0, baseColor),
            nvrhi::BindingSetItem::Texture_SRV(1, metalRough),
            nvrhi::BindingSetItem::Texture_SRV(2, normal),
            nvrhi::BindingSetItem::Texture_SRV(3, occlusion),
            nvrhi::BindingSetItem::Sampler(0, m_sampler),
        };

        nvrhi::BindingSetHandle set = m_bindingCache->GetOrCreateBindingSet(desc, m_bindingLayout);
        return set;
    }

    nvrhi::IGraphicsPipeline* GBufferPass::GetOrCreatePipeline(PipelineKey key, nvrhi::IFramebuffer* framebuffer)
    {
        if (key.value >= kPipelineCount)
        {
            return nullptr;
        }

        nvrhi::GraphicsPipelineHandle& pipeline = m_pipelines[key.value];
        if (pipeline)
        {
            return pipeline;
        }

        const bool frontCounterClockwise = (key.value & 1u) != 0;
        const bool twoSided = (key.value & 2u) != 0;
        GBufferRasterKey rasterKey;
        rasterKey.frontCounterClockwise = frontCounterClockwise;
        rasterKey.twoSided = twoSided;

        nvrhi::GraphicsPipelineDesc desc;
        desc.primType = nvrhi::PrimitiveType::TriangleList;
        desc.inputLayout = m_inputLayout;
        desc.VS = m_vertexShader;
        desc.PS = m_pixelShader;
        desc.bindingLayouts = { m_bindingLayout };
        desc.renderState.rasterState = MakeGBufferRasterState(rasterKey);
        desc.renderState.depthStencilState = MakeGBufferDepthState();
        desc.renderState.blendState.disableAlphaToCoverage();

        pipeline = m_device->createGraphicsPipeline(desc, framebuffer->getFramebufferInfo());
        if (!pipeline)
        {
            log::error(
                "Failed to create GBuffer pipeline (frontCounterClockwise=%s twoSided=%s).",
                frontCounterClockwise ? "true" : "false",
                twoSided ? "true" : "false");
        }
        return pipeline;
    }

    void GBufferPass::Execute(
        nvrhi::ICommandList* commandList,
        const GBufferPassInputs& inputs,
        const GBufferPassOutputs& outputs)
    {
        if (!commandList || !m_device)
        {
            return;
        }

        commandList->beginMarker("GBuffer"); // matches renderlab::markers::kGBuffer

        int timingIndex = -1;
        for (uint32_t offset = 0; offset < kGBufferTimerQueryCount; ++offset)
        {
            const uint32_t index = (m_timerIndex + offset) % kGBufferTimerQueryCount;
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
            scope.nextIndex = (static_cast<uint32_t>(timingIndex) + 1) % kGBufferTimerQueryCount;
        }

        m_hud.drawCount = 0;
        m_hud.skippedMissingBufferCount = 0;

        const bool outputsValid = outputs.gbufferA && outputs.gbufferB && outputs.gbufferC && outputs.gbufferDepth;
        if (!outputsValid)
        {
            log::error("GBufferPass outputs are incomplete; skipping the opaque pass.");
            return;
        }

        commandList->clearTextureFloat(
            outputs.gbufferA, nvrhi::AllSubresources, kGBufferFormats[static_cast<size_t>(GBufferTarget::A)].clearColor);
        commandList->clearTextureFloat(
            outputs.gbufferB, nvrhi::AllSubresources, kGBufferFormats[static_cast<size_t>(GBufferTarget::B)].clearColor);
        commandList->clearTextureFloat(
            outputs.gbufferC, nvrhi::AllSubresources, kGBufferFormats[static_cast<size_t>(GBufferTarget::C)].clearColor);
        commandList->clearDepthStencilTexture(
            outputs.gbufferDepth,
            nvrhi::AllSubresources,
            true,
            kGBufferFormats[static_cast<size_t>(GBufferTarget::Depth)].clearColor.r,
            false,
            0);

        if (!EnsureFramebuffer(outputs))
        {
            return;
        }

        FrameConstants frame = inputs.frameConstants ? *inputs.frameConstants : FrameConstants{};
        ViewConstants view = inputs.viewConstants ? *inputs.viewConstants : ViewConstants{};
        commandList->writeBuffer(m_frameCB, &frame, sizeof(frame));
        commandList->writeBuffer(m_viewCB, &view, sizeof(view));

        const SceneDrawList* draws = inputs.sceneDraws;
        if (!draws || draws->draws.empty())
        {
            return;
        }

        uint32_t dummyVerticesNeeded = 0;
        for (const DrawRecord& draw : draws->draws)
        {
            if (!draw.geometry.texCoord.present || !draw.geometry.tangent.present)
            {
                dummyVerticesNeeded = std::max(
                    dummyVerticesNeeded, draw.geometry.vertexOffset + draw.geometry.vertexCount);
            }
        }
        if (dummyVerticesNeeded > 0 && !EnsureDummyVertexCapacity(commandList, dummyVerticesNeeded))
        {
            return;
        }

        const bool mirrored = (view.flags & RendererViewFlag_Mirrored) != 0;
        const nvrhi::Viewport viewport(
            view.viewportOrigin.x,
            view.viewportOrigin.x + view.viewportSize.x,
            view.viewportOrigin.y,
            view.viewportOrigin.y + view.viewportSize.y,
            0.f,
            1.f);

        for (const DrawRecord& draw : draws->draws)
        {
            if (!draw.geometry.vertexBuffer || !draw.geometry.indexBuffer || draw.geometry.indexCount == 0)
            {
                ++m_hud.skippedMissingBufferCount;
                continue;
            }
            if (!draw.geometry.position.present || !draw.geometry.normal.present)
            {
                ++m_hud.skippedMissingBufferCount;
                continue;
            }

            nvrhi::IGraphicsPipeline* pipeline = GetOrCreatePipeline(
                PipelineKey::From(mirrored, draw.twoSided), m_framebuffer);
            nvrhi::IBindingSet* bindings = GetOrCreateBindingSet(draw);
            if (!pipeline || !bindings)
            {
                ++m_hud.skippedMissingBufferCount;
                continue;
            }

            commandList->writeBuffer(m_instanceCB, &draw.instance, sizeof(draw.instance));
            commandList->writeBuffer(m_materialCB, &draw.material, sizeof(draw.material));

            nvrhi::IBuffer* texCoordBuffer = draw.geometry.texCoord.present ? draw.geometry.vertexBuffer : m_dummyTexCoord.Get();
            nvrhi::IBuffer* tangentBuffer = draw.geometry.tangent.present ? draw.geometry.vertexBuffer : m_dummyTangent.Get();
            const uint64_t texCoordOffset = draw.geometry.texCoord.present ? draw.geometry.texCoord.range.byteOffset : 0;
            const uint64_t tangentOffset = draw.geometry.tangent.present ? draw.geometry.tangent.range.byteOffset : 0;

            nvrhi::GraphicsState state;
            state.pipeline = pipeline;
            state.framebuffer = m_framebuffer;
            state.viewport.addViewportAndScissorRect(viewport);
            state.bindings = { bindings };
            state.vertexBuffers = {
                { draw.geometry.vertexBuffer, 0, draw.geometry.position.range.byteOffset },
                { texCoordBuffer, 1, texCoordOffset },
                { draw.geometry.vertexBuffer, 2, draw.geometry.normal.range.byteOffset },
                { tangentBuffer, 3, tangentOffset },
            };
            state.indexBuffer = {
                draw.geometry.indexBuffer,
                draw.geometry.indexFormat,
                0
            };
            commandList->setGraphicsState(state);

            nvrhi::DrawArguments args;
            args.vertexCount = draw.geometry.indexCount;
            args.instanceCount = 1;
            args.startIndexLocation = draw.geometry.indexOffset;
            args.startVertexLocation = draw.geometry.vertexOffset;
            commandList->drawIndexed(args);
            ++m_hud.drawCount;
        }
    }
}
