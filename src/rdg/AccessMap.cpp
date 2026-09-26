#include "AccessMap.h"

namespace renderlab::rdg
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
        return IsBoundaryAccess(access);
    }
}
