#pragma once

#include "LightingContract.h"

#include <nvrhi/nvrhi.h>

#include <cstdint>
#include <string>

namespace renderlab
{
    struct HDRSceneColorTargetHud
    {
        const char* debugName = kHDRSceneColorDebugName;
        const char* formatName = "";
        nvrhi::Format format = kHDRSceneColorFormat;
        uint32_t bytesPerPixel = kHDRSceneColorBytesPerPixel;
        uint32_t width = 0;
        uint32_t height = 0;
        uint64_t approximateBytes = 0;
        uint32_t createCount = 0;
        uint32_t releaseCount = 0;
        bool valid = false;
    };

    // Persistent HDR scene-color texture. Caller owns lifetime (docs/lighting.md).
    // Resize follows the same Donut/NVRHI sequence as GBufferTargets:
    //   BackBufferResizing -> Release()
    //   BackBufferResized  -> Create()
    class HDRSceneColorTarget
    {
    public:
        bool Create(nvrhi::IDevice* device, uint32_t width, uint32_t height);
        void Release();
        void Clear(nvrhi::ICommandList* commandList) const;

        bool IsValid() const;
        uint32_t GetWidth() const { return m_width; }
        uint32_t GetHeight() const { return m_height; }
        uint64_t GetApproximateBytes() const;

        nvrhi::ITexture* GetTexture() const;
        nvrhi::ITexture* GetShaderResource() const { return GetTexture(); }

        HDRSceneColorTargetHud GetHud() const;
        bool ValidateCreatedResources(std::string& error) const;

    private:
        nvrhi::TextureHandle m_texture;
        uint32_t m_width = 0;
        uint32_t m_height = 0;
        uint32_t m_createCount = 0;
        uint32_t m_releaseCount = 0;
    };
}
