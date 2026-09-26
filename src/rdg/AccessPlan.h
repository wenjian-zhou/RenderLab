#pragma once

#include "Pass.h"

#include <cstdint>
#include <string>
#include <vector>

namespace renderlab::rdg
{
    struct ResourceBoundary
    {
        uint32_t resourceIndex = 0;
        std::string name;
        Access initial = Access::Unknown;
        Access final = Access::Unknown;
        bool imported = false;
        bool exported = false;
    };

    struct PassResourceState
    {
        uint32_t passIndex = 0;
        std::string passName;
        uint32_t resourceIndex = 0;
        std::string resourceName;
        ResourceKind kind = ResourceKind::Texture;
        Access before = Access::Unknown;
        Access required = Access::Unknown;
        Access after = Access::Unknown;
    };

    struct ResourceRestore
    {
        uint32_t resourceIndex = 0;
        std::string name;
        Access from = Access::Unknown;
        Access to = Access::Unknown;
    };

    struct AccessPlan
    {
        std::vector<ResourceBoundary> resources;
        std::vector<PassResourceState> passes;
        std::vector<ResourceRestore> restores;
        std::string Dump() const;
    };
}
