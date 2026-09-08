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
        Raster = 1u << 0,
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

    // One read/write declaration. S4.1 records declarations verbatim — no
    // dedup, no conflict rules; those are S4.2/S4.3 semantics. version is
    // the handle's version, pinned at declaration time.
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
