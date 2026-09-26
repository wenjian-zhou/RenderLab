#pragma once

#include "Pass.h"

#include <nvrhi/nvrhi.h>

namespace renderlab::rdg
{
    nvrhi::ResourceStates ToNvStates(Access access);
    Access FromNvStates(nvrhi::ResourceStates states);
    bool IsSingleKnownAccess(Access access);
}
