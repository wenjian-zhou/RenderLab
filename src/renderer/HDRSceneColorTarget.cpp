#include "HDRSceneColorTarget.h"

#include <donut/core/log.h>

#include <cstdio>

using namespace donut;

namespace renderlab
{
    namespace
    {
        const char* FormatName(nvrhi::Format format)
        {
            return nvrhi::getFormatInfo(format).name;
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

    bool HDRSceneColorTarget::Create(nvrhi::IDevice* device, uint32_t width, uint32_t height)
    {
        if (!device)
        {
            log::error("HDRSceneColor create requires an NVRHI device.");
            return false;
        }
        if (width == 0 || height == 0)
        {
            log::error("HDRSceneColor create rejected a zero-sized target (%u x %u).", width, height);
            return false;
        }

        if (IsValid() && m_width == width && m_height == height)
        {
            return true;
        }

        Release();

        const nvrhi::TextureDesc desc = MakeHDRSceneColorTextureDesc(width, height);
        m_texture = device->createTexture(desc);
        if (!m_texture)
        {
            log::error(
                "Failed to create %s (%s, %u x %u).",
                kHDRSceneColorDebugName,
                FormatName(desc.format),
                width,
                height);
            return false;
        }

        m_width = width;
        m_height = height;
        ++m_createCount;

        std::string validationError;
        if (!ValidateCreatedResources(validationError))
        {
            log::error(
                "HDRSceneColor created resources failed contract validation: %s",
                validationError.c_str());
            Release();
            return false;
        }

        log::info(
            "HDRSceneColor created %u x %u sampleCount=1 mipLevels=1 approxBytes=%llu format=%s "
            "createCount=%u",
            m_width,
            m_height,
            static_cast<unsigned long long>(GetApproximateBytes()),
            FormatName(kHDRSceneColorFormat),
            m_createCount);
        return true;
    }

    void HDRSceneColorTarget::Release()
    {
        if (m_texture)
        {
            ++m_releaseCount;
            log::info(
                "HDRSceneColor released %u x %u (releaseCount=%u). Previous resources stay alive "
                "until NVRHI command-list refs are collected after DeviceManager waitForIdle.",
                m_width,
                m_height,
                m_releaseCount);
            m_texture = nullptr;
        }

        m_width = 0;
        m_height = 0;
    }

    void HDRSceneColorTarget::Clear(nvrhi::ICommandList* commandList) const
    {
        if (!commandList || !IsValid())
        {
            return;
        }

        commandList->clearTextureFloat(m_texture, nvrhi::AllSubresources, kHDRSceneColorClear);
    }

    bool HDRSceneColorTarget::IsValid() const
    {
        return m_texture != nullptr && m_width > 0 && m_height > 0;
    }

    uint64_t HDRSceneColorTarget::GetApproximateBytes() const
    {
        if (!IsValid())
        {
            return 0;
        }
        return EstimateHDRSceneColorBytes(m_width, m_height);
    }

    nvrhi::ITexture* HDRSceneColorTarget::GetTexture() const
    {
        return m_texture;
    }

    HDRSceneColorTargetHud HDRSceneColorTarget::GetHud() const
    {
        HDRSceneColorTargetHud hud;
        hud.debugName = kHDRSceneColorDebugName;
        hud.format = kHDRSceneColorFormat;
        hud.formatName = FormatName(kHDRSceneColorFormat);
        hud.bytesPerPixel = kHDRSceneColorBytesPerPixel;
        hud.width = m_width;
        hud.height = m_height;
        hud.approximateBytes = GetApproximateBytes();
        hud.createCount = m_createCount;
        hud.releaseCount = m_releaseCount;
        hud.valid = IsValid();
        return hud;
    }

    bool HDRSceneColorTarget::ValidateCreatedResources(std::string& error) const
    {
        error.clear();
        if (!IsValid())
        {
            error = "HDRSceneColor target is not created";
            return false;
        }

        const nvrhi::TextureDesc& desc = m_texture->getDesc();
        if (!TextureDescMatchesHDRSceneColorContract(desc, error))
        {
            return false;
        }
        if (desc.width != m_width || desc.height != m_height)
        {
            char buffer[128] = {};
            std::snprintf(
                buffer,
                sizeof(buffer),
                "size is %u x %u, expected %u x %u",
                desc.width,
                desc.height,
                m_width,
                m_height);
            AppendError(error, buffer);
        }
        return error.empty();
    }
}
