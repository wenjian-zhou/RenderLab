#include "PhysicalRegistry.h"

#include <utility>

namespace renderlab::rdg
{
    void PhysicalRegistry::Reset(size_t resourceCount)
    {
        m_textures.assign(resourceCount, std::nullopt);
        m_buffers.assign(resourceCount, std::nullopt);
    }

    bool PhysicalRegistry::HasTexture(uint32_t index) const
    {
        return index < m_textures.size() && m_textures[index].has_value();
    }

    bool PhysicalRegistry::HasBuffer(uint32_t index) const
    {
        return index < m_buffers.size() && m_buffers[index].has_value();
    }

    void PhysicalRegistry::SetTexture(uint32_t index, PhysicalTexture physical)
    {
        m_textures[index] = std::move(physical);
    }

    void PhysicalRegistry::SetBuffer(uint32_t index, PhysicalBuffer physical)
    {
        m_buffers[index] = std::move(physical);
    }

    const PhysicalTexture* PhysicalRegistry::GetTexture(uint32_t index) const
    {
        if (!HasTexture(index))
        {
            return nullptr;
        }
        return &*m_textures[index];
    }

    const PhysicalBuffer* PhysicalRegistry::GetBuffer(uint32_t index) const
    {
        if (!HasBuffer(index))
        {
            return nullptr;
        }
        return &*m_buffers[index];
    }
}
