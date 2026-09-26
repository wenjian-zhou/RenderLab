#pragma once

#include "Handle.h"

#include <cstdint>
#include <string>
#include <vector>

namespace renderlab::rdg
{
    enum class PassFlags : uint32_t
    {
        None = 0,
        Raster = 1u << 0, // dumps print it; access comes from Use, not this flag
        // Side-effect flag: the pass and its producers must survive culling
        // (S4.4). Mirrors UE ERDGPassFlags::NeverCull (ue-rdg-survey §4).
        NeverCull = 1u << 1,
    };

    constexpr PassFlags operator|(PassFlags a, PassFlags b)
    {
        return static_cast<PassFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
    }

    constexpr PassFlags operator&(PassFlags a, PassFlags b)
    {
        return static_cast<PassFlags>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
    }

    constexpr PassFlags& operator|=(PassFlags& a, PassFlags b)
    {
        return a = a | b;
    }

    // Raster access subset. Present is a graph-boundary state only.
    enum class Access : uint32_t
    {
        Unknown = 0,
        ShaderResource = 1u << 0,
        RenderTarget = 1u << 1,
        DepthWrite = 1u << 2,
        Present = 1u << 3,
    };

    constexpr Access operator|(Access a, Access b)
    {
        return static_cast<Access>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
    }

    constexpr Access operator&(Access a, Access b)
    {
        return static_cast<Access>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
    }

    constexpr Access& operator|=(Access& a, Access b)
    {
        return a = a | b;
    }

    constexpr Access kKnownAccessBits =
        Access::ShaderResource | Access::RenderTarget | Access::DepthWrite | Access::Present;
    constexpr Access kWritableMask = Access::RenderTarget | Access::DepthWrite;
    constexpr Access kReadableMask = Access::ShaderResource;
    constexpr Access kPassUseBits = Access::ShaderResource | Access::RenderTarget | Access::DepthWrite;

    constexpr bool IsSingleAccessBit(Access access)
    {
        const uint32_t bits = static_cast<uint32_t>(access);
        return bits != 0 && (bits & (bits - 1u)) == 0;
    }

    constexpr bool IsPassUseAccess(Access access)
    {
        const uint32_t bits = static_cast<uint32_t>(access);
        const uint32_t allowed = static_cast<uint32_t>(kPassUseBits);
        return IsSingleAccessBit(access) && (bits & ~allowed) == 0;
    }

    constexpr bool IsBoundaryAccess(Access access)
    {
        const uint32_t bits = static_cast<uint32_t>(access);
        const uint32_t allowed = static_cast<uint32_t>(kKnownAccessBits);
        return IsSingleAccessBit(access) && (bits & ~allowed) == 0;
    }

    constexpr bool IsWritableAccess(Access access)
    {
        return (access & kWritableMask) != Access::Unknown;
    }

    constexpr const char* ToString(Access access)
    {
        switch (access)
        {
        case Access::ShaderResource: return "ShaderResource";
        case Access::RenderTarget: return "RenderTarget";
        case Access::DepthWrite: return "DepthWrite";
        case Access::Present: return "Present";
        default: return "Unknown";
        }
    }

    struct ResourceAccess
    {
        ResourceKind kind = ResourceKind::Texture;
        uint32_t index = 0;
        Access access = Access::Unknown;
    };

    struct PassRecord
    {
        std::string name;
        PassFlags flags = PassFlags::None;
        std::vector<ResourceAccess> accesses;
    };
}
