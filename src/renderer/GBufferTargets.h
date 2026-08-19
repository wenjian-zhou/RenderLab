#pragma once

#include "GBufferContract.h"

#include <nvrhi/nvrhi.h>

#include <array>
#include <cstdint>
#include <string>

namespace renderlab
{
    inline constexpr uint32_t kGBufferSampleCount = 1;
    inline constexpr uint32_t kGBufferMipLevels = 1;

    struct GBufferTargetHud
    {
        const char* debugName = "";
        const char* formatName = "";
        nvrhi::Format format = nvrhi::Format::UNKNOWN;
        uint32_t bytesPerPixel = 0;
        uint64_t approximateBytes = 0;
        bool valid = false;
    };

    struct GBufferTargetsHud
    {
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t sampleCount = kGBufferSampleCount;
        uint64_t approximateBytes = 0;
        uint32_t createCount = 0;
        uint32_t releaseCount = 0;
        bool valid = false;
        std::array<GBufferTargetHud, static_cast<size_t>(GBufferTarget::Count)> targets{};
    };

    nvrhi::TextureDesc MakeGBufferTextureDesc(GBufferTarget target, uint32_t width, uint32_t height);
    uint32_t GBufferBytesPerPixel(GBufferTarget target);
    uint64_t EstimateGBufferTargetBytes(GBufferTarget target, uint32_t width, uint32_t height);
    uint64_t EstimateGBufferTotalBytes(uint32_t width, uint32_t height);
    bool TextureDescMatchesGBufferContract(const nvrhi::TextureDesc& desc, GBufferTarget target, std::string& error);

    // Persistent GBuffer/depth textures. Creation is independent of mesh drawing (S1.4).
    // Resize follows the Donut/NVRHI lifetime model:
    //   BackBufferResizing  -> Release() while command lists may still hold refs
    //   DeviceManager DX12  -> waitForIdle() + runGarbageCollection()
    //   BackBufferResized   -> Create() at the new back-buffer size
    class GBufferTargets
    {
    public:
        bool Create(nvrhi::IDevice* device, uint32_t width, uint32_t height);
        void Release();
        void Clear(nvrhi::ICommandList* commandList) const;

        bool IsValid() const;
        uint32_t GetWidth() const { return m_width; }
        uint32_t GetHeight() const { return m_height; }
        uint64_t GetApproximateBytes() const;

        nvrhi::ITexture* GetTexture(GBufferTarget target) const;
        nvrhi::ITexture* GetShaderResource(GBufferTarget target) const;

        GBufferTargetsHud GetHud() const;
        bool ValidateCreatedResources(std::string& error) const;

    private:
        using TextureArray = std::array<nvrhi::TextureHandle, static_cast<size_t>(GBufferTarget::Count)>;

        TextureArray m_textures{};
        uint32_t m_width = 0;
        uint32_t m_height = 0;
        uint32_t m_createCount = 0;
        uint32_t m_releaseCount = 0;
    };
}
