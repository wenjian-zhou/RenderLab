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
        Raster = 1u << 0, // informational; S4.6 dumps consume it, S5.3 plans access
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

    enum class AccessMode : uint8_t
    {
        Read,
        Write,
    };

    // Raster access subset (S5.3). Exec infers these from Read/Write + format.
    // Present is a graph-boundary state only; UAV is deferred.
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

    struct ResourceAccess
    {
        ResourceKind kind = ResourceKind::Texture;
        uint32_t index = 0;
        uint32_t version = 0;
        AccessMode mode = AccessMode::Read;
    };

    struct PassRecord
    {
        std::string name;
        PassFlags flags = PassFlags::None;
        std::vector<ResourceAccess> accesses;
    };
}
