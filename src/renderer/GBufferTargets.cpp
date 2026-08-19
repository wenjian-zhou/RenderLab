#include "GBufferTargets.h"

#include <donut/core/log.h>

#include <cstdio>

using namespace donut;

namespace renderlab
{
    namespace
    {
        constexpr size_t TargetIndex(GBufferTarget target)
        {
            return static_cast<size_t>(target);
        }

        bool IsDepthTarget(GBufferTarget target)
        {
            return target == GBufferTarget::Depth;
        }

        const char* FormatName(nvrhi::Format format)
        {
            return nvrhi::getFormatInfo(format).name;
        }

        bool ColorsEqual(const nvrhi::Color& actual, const nvrhi::Color& expected)
        {
            return actual == expected;
        }

        void AppendError(std::string& error, const char* text)
        {
            if (!error.empty())
            {
                error += "; ";
            }
            error += text;
        }
    }

    static_assert(TargetIndex(GBufferTarget::Count) == 4);
    static_assert(sizeof(kGBufferFormats) / sizeof(kGBufferFormats[0]) == 4);

    nvrhi::TextureDesc MakeGBufferTextureDesc(GBufferTarget target, uint32_t width, uint32_t height)
    {
        const GBufferFormatDesc& format = kGBufferFormats[TargetIndex(target)];

        nvrhi::TextureDesc desc;
        desc.width = width;
        desc.height = height;
        desc.depth = 1;
        desc.arraySize = 1;
        desc.mipLevels = kGBufferMipLevels;
        desc.sampleCount = kGBufferSampleCount;
        desc.sampleQuality = 0;
        desc.format = format.format;
        desc.dimension = nvrhi::TextureDimension::Texture2D;
        desc.debugName = format.debugName;
        desc.isShaderResource = true;
        desc.isRenderTarget = true;
        desc.isUAV = false;
        desc.isTypeless = format.typeless;
        desc.isShadingRateSurface = false;
        desc.isVirtual = false;
        desc.isTiled = false;
        desc.clearValue = format.clearColor;
        desc.useClearValue = true;
        desc.initialState = IsDepthTarget(target)
            ? nvrhi::ResourceStates::DepthWrite
            : nvrhi::ResourceStates::RenderTarget;
        desc.keepInitialState = true;
        return desc;
    }

    uint32_t GBufferBytesPerPixel(GBufferTarget target)
    {
        const nvrhi::Format format = kGBufferFormats[TargetIndex(target)].format;
        const nvrhi::FormatInfo& info = nvrhi::getFormatInfo(format);
        return static_cast<uint32_t>(info.bytesPerBlock / info.blockSize / info.blockSize);
    }

    uint64_t EstimateGBufferTargetBytes(GBufferTarget target, uint32_t width, uint32_t height)
    {
        return static_cast<uint64_t>(width) * static_cast<uint64_t>(height) *
               static_cast<uint64_t>(GBufferBytesPerPixel(target));
    }

    uint64_t EstimateGBufferTotalBytes(uint32_t width, uint32_t height)
    {
        uint64_t total = 0;
        for (uint32_t index = 0; index < static_cast<uint32_t>(GBufferTarget::Count); ++index)
        {
            total += EstimateGBufferTargetBytes(static_cast<GBufferTarget>(index), width, height);
        }
        return total;
    }

    bool TextureDescMatchesGBufferContract(
        const nvrhi::TextureDesc& desc,
        GBufferTarget target,
        std::string& error)
    {
        error.clear();
        const GBufferFormatDesc& expected = kGBufferFormats[TargetIndex(target)];
        const bool depth = IsDepthTarget(target);

        if (desc.debugName != expected.debugName)
        {
            AppendError(error, "debug name mismatch");
        }
        if (desc.format != expected.format)
        {
            AppendError(error, "format mismatch");
        }
        if (desc.isTypeless != expected.typeless)
        {
            AppendError(error, "typeless flag mismatch");
        }
        if (!desc.isRenderTarget)
        {
            AppendError(error, "isRenderTarget must be true");
        }
        if (!desc.isShaderResource)
        {
            AppendError(error, "isShaderResource must be true");
        }
        if (desc.isUAV)
        {
            AppendError(error, "isUAV must be false");
        }
        if (!desc.useClearValue)
        {
            AppendError(error, "useClearValue must be true");
        }
        if (!ColorsEqual(desc.clearValue, expected.clearColor))
        {
            AppendError(error, "clear value mismatch");
        }
        if (desc.mipLevels != kGBufferMipLevels)
        {
            AppendError(error, "mipLevels must be 1");
        }
        if (desc.sampleCount != kGBufferSampleCount)
        {
            AppendError(error, "sampleCount must be 1");
        }
        if (desc.dimension != nvrhi::TextureDimension::Texture2D)
        {
            AppendError(error, "dimension must be Texture2D");
        }
        const nvrhi::ResourceStates expectedState = depth
            ? nvrhi::ResourceStates::DepthWrite
            : nvrhi::ResourceStates::RenderTarget;
        if (desc.initialState != expectedState)
        {
            AppendError(error, "initialState mismatch");
        }
        if (!desc.keepInitialState)
        {
            AppendError(error, "keepInitialState must be true for NVRHI automatic tracking");
        }

        return error.empty();
    }

    bool GBufferTargets::Create(nvrhi::IDevice* device, uint32_t width, uint32_t height)
    {
        if (!device)
        {
            log::error("GBuffer create requires an NVRHI device.");
            return false;
        }
        if (width == 0 || height == 0)
        {
            log::error("GBuffer create rejected a zero-sized target (%u x %u).", width, height);
            return false;
        }

        if (IsValid() && m_width == width && m_height == height)
        {
            return true;
        }

        Release();

        TextureArray created{};
        for (uint32_t index = 0; index < static_cast<uint32_t>(GBufferTarget::Count); ++index)
        {
            const GBufferTarget target = static_cast<GBufferTarget>(index);
            const nvrhi::TextureDesc desc = MakeGBufferTextureDesc(target, width, height);
            created[index] = device->createTexture(desc);
            if (!created[index])
            {
                log::error(
                    "Failed to create %s (%s, %u x %u).",
                    kGBufferFormats[index].debugName,
                    FormatName(desc.format),
                    width,
                    height);
                return false;
            }
        }

        m_textures = std::move(created);
        m_width = width;
        m_height = height;
        ++m_createCount;

        std::string validationError;
        if (!ValidateCreatedResources(validationError))
        {
            log::error("GBuffer created resources failed contract validation: %s", validationError.c_str());
            Release();
            return false;
        }

        log::info(
            "GBuffer created %u x %u sampleCount=%u mipLevels=%u approxBytes=%llu "
            "A=%s B=%s C=%s Depth=%s (typeless D32 DSV+SRV) createCount=%u",
            m_width,
            m_height,
            kGBufferSampleCount,
            kGBufferMipLevels,
            static_cast<unsigned long long>(GetApproximateBytes()),
            FormatName(kGBufferFormats[TargetIndex(GBufferTarget::A)].format),
            FormatName(kGBufferFormats[TargetIndex(GBufferTarget::B)].format),
            FormatName(kGBufferFormats[TargetIndex(GBufferTarget::C)].format),
            FormatName(kGBufferFormats[TargetIndex(GBufferTarget::Depth)].format),
            m_createCount);
        return true;
    }

    void GBufferTargets::Release()
    {
        bool hadResources = false;
        for (nvrhi::TextureHandle& texture : m_textures)
        {
            if (texture)
            {
                hadResources = true;
                texture = nullptr;
            }
        }

        if (hadResources)
        {
            ++m_releaseCount;
            log::info(
                "GBuffer released %u x %u targets (releaseCount=%u). Previous resources stay alive until "
                "NVRHI command-list refs are collected after DeviceManager waitForIdle.",
                m_width,
                m_height,
                m_releaseCount);
        }

        m_width = 0;
        m_height = 0;
    }

    void GBufferTargets::Clear(nvrhi::ICommandList* commandList) const
    {
        if (!commandList || !IsValid())
        {
            return;
        }

        for (uint32_t index = 0; index < static_cast<uint32_t>(GBufferTarget::Count); ++index)
        {
            nvrhi::ITexture* texture = m_textures[index];
            if (!texture)
            {
                continue;
            }

            const GBufferTarget target = static_cast<GBufferTarget>(index);
            const nvrhi::Color& clearColor = kGBufferFormats[index].clearColor;
            if (IsDepthTarget(target))
            {
                commandList->clearDepthStencilTexture(
                    texture,
                    nvrhi::AllSubresources,
                    true,
                    clearColor.r,
                    false,
                    0);
            }
            else
            {
                commandList->clearTextureFloat(texture, nvrhi::AllSubresources, clearColor);
            }
        }
    }

    bool GBufferTargets::IsValid() const
    {
        if (m_width == 0 || m_height == 0)
        {
            return false;
        }
        for (const nvrhi::TextureHandle& texture : m_textures)
        {
            if (!texture)
            {
                return false;
            }
        }
        return true;
    }

    uint64_t GBufferTargets::GetApproximateBytes() const
    {
        if (!IsValid())
        {
            return 0;
        }
        return EstimateGBufferTotalBytes(m_width, m_height);
    }

    nvrhi::ITexture* GBufferTargets::GetTexture(GBufferTarget target) const
    {
        return m_textures[TargetIndex(target)];
    }

    nvrhi::ITexture* GBufferTargets::GetShaderResource(GBufferTarget target) const
    {
        // NVRHI creates the SRV from TextureDesc (depth: R32_FLOAT on the typeless D32 allocation).
        return GetTexture(target);
    }

    GBufferTargetsHud GBufferTargets::GetHud() const
    {
        GBufferTargetsHud hud;
        hud.width = m_width;
        hud.height = m_height;
        hud.sampleCount = kGBufferSampleCount;
        hud.approximateBytes = GetApproximateBytes();
        hud.createCount = m_createCount;
        hud.releaseCount = m_releaseCount;
        hud.valid = IsValid();

        for (uint32_t index = 0; index < static_cast<uint32_t>(GBufferTarget::Count); ++index)
        {
            const GBufferTarget target = static_cast<GBufferTarget>(index);
            GBufferTargetHud& item = hud.targets[index];
            item.debugName = kGBufferFormats[index].debugName;
            item.format = kGBufferFormats[index].format;
            item.formatName = FormatName(item.format);
            item.bytesPerPixel = GBufferBytesPerPixel(target);
            item.approximateBytes = EstimateGBufferTargetBytes(target, m_width, m_height);
            item.valid = m_textures[index] != nullptr;
        }
        return hud;
    }

    bool GBufferTargets::ValidateCreatedResources(std::string& error) const
    {
        error.clear();
        if (!IsValid())
        {
            error = "GBuffer targets are not created";
            return false;
        }

        for (uint32_t index = 0; index < static_cast<uint32_t>(GBufferTarget::Count); ++index)
        {
            const GBufferTarget target = static_cast<GBufferTarget>(index);
            const nvrhi::TextureDesc& desc = m_textures[index]->getDesc();
            std::string targetError;
            if (!TextureDescMatchesGBufferContract(desc, target, targetError))
            {
                AppendError(error, kGBufferFormats[index].debugName);
                error += ": ";
                error += targetError;
            }
            if (desc.width != m_width || desc.height != m_height)
            {
                char buffer[128] = {};
                std::snprintf(
                    buffer,
                    sizeof(buffer),
                    "%s size is %u x %u, expected %u x %u",
                    kGBufferFormats[index].debugName,
                    desc.width,
                    desc.height,
                    m_width,
                    m_height);
                AppendError(error, buffer);
            }
        }

        return error.empty();
    }
}
