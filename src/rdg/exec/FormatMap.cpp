#include "FormatMap.h"

namespace renderlab::rdg::exec
{
    nvrhi::Format ToNvFormat(Format format)
    {
        switch (format)
        {
        case Format::SRGBA8Unorm: return nvrhi::Format::SRGBA8_UNORM;
        case Format::RGBA8Unorm: return nvrhi::Format::RGBA8_UNORM;
        case Format::RGBA16Float: return nvrhi::Format::RGBA16_FLOAT;
        case Format::RGBA32Float: return nvrhi::Format::RGBA32_FLOAT;
        case Format::R32Float: return nvrhi::Format::R32_FLOAT;
        case Format::D32Float: return nvrhi::Format::D32;
        case Format::Unknown: break;
        }
        return nvrhi::Format::UNKNOWN;
    }

    Format FromNvFormat(nvrhi::Format format)
    {
        switch (format)
        {
        case nvrhi::Format::SRGBA8_UNORM: return Format::SRGBA8Unorm;
        case nvrhi::Format::RGBA8_UNORM: return Format::RGBA8Unorm;
        case nvrhi::Format::RGBA16_FLOAT: return Format::RGBA16Float;
        case nvrhi::Format::RGBA32_FLOAT: return Format::RGBA32Float;
        case nvrhi::Format::R32_FLOAT: return Format::R32Float;
        case nvrhi::Format::D32: return Format::D32Float;
        default: return Format::Unknown;
        }
    }

    uint32_t BytesPerPixel(Format format)
    {
        switch (format)
        {
        case Format::SRGBA8Unorm:
        case Format::RGBA8Unorm:
        case Format::R32Float:
        case Format::D32Float:
            return 4;
        case Format::RGBA16Float:
            return 8;
        case Format::RGBA32Float:
            return 16;
        case Format::Unknown:
            break;
        }
        return 0;
    }

    nvrhi::TextureDesc MakeTextureDesc(const TextureDesc& desc)
    {
        nvrhi::TextureDesc nv;
        nv.width = desc.width;
        nv.height = desc.height;
        nv.depth = 1;
        nv.arraySize = 1;
        nv.mipLevels = 1;
        nv.sampleCount = 1;
        nv.sampleQuality = 0;
        nv.format = ToNvFormat(desc.format);
        nv.dimension = nvrhi::TextureDimension::Texture2D;
        nv.debugName = desc.name;
        nv.isShaderResource = true;
        nv.isRenderTarget = true;
        nv.isUAV = false;
        nv.useClearValue = true;
        nv.keepInitialState = true;

        if (desc.format == Format::D32Float)
        {
            nv.isTypeless = true;
            nv.initialState = nvrhi::ResourceStates::DepthWrite;
            nv.clearValue = nvrhi::Color(0.f);
        }
        else
        {
            nv.isTypeless = false;
            nv.initialState = nvrhi::ResourceStates::RenderTarget;
            if (desc.format == Format::SRGBA8Unorm || desc.format == Format::RGBA8Unorm)
            {
                nv.clearValue = nvrhi::Color(0.f, 0.f, 0.f, 1.f);
            }
            else
            {
                nv.clearValue = nvrhi::Color(0.f, 0.f, 0.f, 0.f);
            }
        }

        return nv;
    }

    nvrhi::BufferDesc MakeBufferDesc(const BufferDesc& desc)
    {
        nvrhi::BufferDesc nv;
        nv.byteSize = static_cast<uint64_t>(desc.bytesPerElement) * desc.numElements;
        nv.structStride = desc.bytesPerElement;
        nv.debugName = desc.name;
        nv.canHaveRawViews = true;
        nv.initialState = nvrhi::ResourceStates::ShaderResource;
        nv.keepInitialState = true;
        return nv;
    }
}
