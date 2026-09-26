#pragma once

#include "PhysicalResource.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace renderlab::rdg
{
    // Per-resource-index slots for physical tokens. Imports are filled by
    // RegisterImport; internals are filled by GraphExecutor::Allocate.
    class PhysicalRegistry
    {
    public:
        void Reset(size_t resourceCount);

        bool HasTexture(uint32_t index) const;
        bool HasBuffer(uint32_t index) const;

        void SetTexture(uint32_t index, PhysicalTexture physical);
        void SetBuffer(uint32_t index, PhysicalBuffer physical);

        const PhysicalTexture* GetTexture(uint32_t index) const;
        const PhysicalBuffer* GetBuffer(uint32_t index) const;

    private:
        std::vector<std::optional<PhysicalTexture>> m_textures;
        std::vector<std::optional<PhysicalBuffer>> m_buffers;
    };
}
