#pragma once

#include "../Handle.h"
#include "../Pass.h"
#include "PhysicalResource.h"

#include <cstdint>
#include <span>

namespace renderlab::rdg::exec
{
    class GraphExecutor;

    // Per-pass resolve surface. Valid only while GraphExecutor::Execute is
    // invoking that pass's lambda. GetTexture/GetBuffer resolve declared
    // handles only; they do not expose the registry, compile result, or
    // command lists.
    class PassContext
    {
    public:
        PassContext(const PassContext&) = delete;
        PassContext& operator=(const PassContext&) = delete;

        const PhysicalTexture* GetTexture(TextureHandle handle);
        const PhysicalBuffer* GetBuffer(BufferHandle handle);

    private:
        friend class GraphExecutor;

        PassContext() = default;

        void Activate(GraphExecutor& executor, uint32_t passIndex, std::span<const ResourceAccess> accesses);
        void Deactivate();

        GraphExecutor* m_executor = nullptr;
        uint32_t m_passIndex = 0;
        std::span<const ResourceAccess> m_accesses{};
        bool m_active = false;
    };
}
