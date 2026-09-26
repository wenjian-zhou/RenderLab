#pragma once

#include "ResourceDesc.h"

#include <nvrhi/nvrhi.h>

namespace renderlab::rdg
{
    nvrhi::Format ToNvFormat(Format format);
    Format FromNvFormat(nvrhi::Format format);
    uint32_t BytesPerPixel(Format format);
    nvrhi::TextureDesc MakeTextureDesc(const TextureDesc& desc);
    nvrhi::BufferDesc MakeBufferDesc(const BufferDesc& desc);
}
