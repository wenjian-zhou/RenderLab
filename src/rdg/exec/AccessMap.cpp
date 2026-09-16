#include "AccessMap.h"

namespace renderlab::rdg::exec
{
    nvrhi::ResourceStates ToNvStates(Access access)
    {
        switch (access)
        {
        case Access::ShaderResource: return nvrhi::ResourceStates::ShaderResource;
        case Access::RenderTarget: return nvrhi::ResourceStates::RenderTarget;
        case Access::DepthWrite: return nvrhi::ResourceStates::DepthWrite;
        case Access::Present: return nvrhi::ResourceStates::Present;
        default: return nvrhi::ResourceStates::Unknown;
        }
    }

    Access FromNvStates(nvrhi::ResourceStates states)
    {
        switch (states)
        {
        case nvrhi::ResourceStates::ShaderResource: return Access::ShaderResource;
        case nvrhi::ResourceStates::RenderTarget: return Access::RenderTarget;
        case nvrhi::ResourceStates::DepthWrite: return Access::DepthWrite;
        case nvrhi::ResourceStates::Present: return Access::Present;
        default: return Access::Unknown;
        }
    }

    bool IsSingleKnownAccess(Access access)
    {
        const uint32_t bits = static_cast<uint32_t>(access);
        const uint32_t known = static_cast<uint32_t>(kKnownAccessBits);
        return bits != 0 && (bits & known) == bits && (bits & (bits - 1u)) == 0;
    }

    Access InferAccess(AccessMode mode, ResourceKind kind, Format format)
    {
        if (mode == AccessMode::Read)
        {
            return Access::ShaderResource;
        }
        if (kind == ResourceKind::Buffer)
        {
            return Access::Unknown;
        }
        if (format == Format::D32Float)
        {
            return Access::DepthWrite;
        }
        if (format == Format::Unknown)
        {
            return Access::Unknown;
        }
        return Access::RenderTarget;
    }
}
