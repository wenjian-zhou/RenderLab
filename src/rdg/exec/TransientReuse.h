#pragma once

#include "../GraphBuilder.h"
#include "../GraphCompiler.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace renderlab::rdg::exec
{
    // Sentinel in TransientReusePlan::physicalOwner. Imported resources and
    // unmappable textures stay here; Allocate skips or reports them itself.
    inline constexpr uint32_t kUnallocated = 0xFFFFFFFFu;

    struct ReusePair
    {
        uint32_t ownerIndex = 0;
        uint32_t aliasIndex = 0;
        std::string ownerName;
        std::string aliasName;
    };

    // physicalOwner[i] == i: this Create* owns a physical object.
    // physicalOwner[i] == owner: alias of that owner (owner is the first
    // resource that created the object, not the previous alias in a chain).
    // physicalOwner[i] == kUnallocated: not created by the reuse plan.
    struct TransientReusePlan
    {
        std::vector<uint32_t> physicalOwner;
        std::vector<ReusePair> reusePairs;
    };

    // Exact descriptor match, ignoring debug names. Overlap is the closed
    // live-slot interval, not a numeric compare of pass indices. Imported
    // and exported resources are ineligible. reuseEnabled false makes every
    // eligible Create* its own owner.
    TransientReusePlan PlanTransientReuse(
        const GraphBuilder& builder,
        std::span<const ResourceLifetime> lifetimes,
        std::span<const uint32_t> livePassOrder,
        bool reuseEnabled);
}
