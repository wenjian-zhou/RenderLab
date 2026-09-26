#pragma once

#include "Pass.h"
#include "ResourceDesc.h"

#include <nvrhi/nvrhi.h>

namespace renderlab::rdg
{
    nvrhi::ResourceStates ToNvStates(Access access);
    Access FromNvStates(nvrhi::ResourceStates states);
    bool IsSingleKnownAccess(Access access);
    Access InferAccess(AccessMode mode, ResourceKind kind, Format format);
}
