#include "PassContext.h"
#include "GraphExecutor.h"

#include <format>

namespace renderlab::rdg
{
    namespace
    {
        const ResourceAccess* FindDeclaredAccess(
            std::span<const ResourceAccess> accesses,
            ResourceKind kind,
            uint32_t index)
        {
            for (const ResourceAccess& access : accesses)
            {
                if (access.kind == kind && access.index == index)
                {
                    return &access;
                }
            }
            return nullptr;
        }
    }

    void PassContext::Activate(
        GraphExecutor& executor,
        uint32_t passIndex,
        std::span<const ResourceAccess> accesses)
    {
        m_executor = &executor;
        m_passIndex = passIndex;
        m_accesses = accesses;
        m_active = true;
    }

    void PassContext::Deactivate()
    {
        m_active = false;
        m_accesses = {};
    }

    const PhysicalTexture* PassContext::GetTexture(TextureHandle handle)
    {
        if (!m_active)
        {
            m_executor->AddError(
                ErrorCategory::ExpiredContext,
                "GetTexture after the pass has ended",
                m_passIndex,
                m_executor->m_builder->GetPass(m_passIndex).name,
                "");
            return nullptr;
        }

        const PassRecord& pass = m_executor->m_builder->GetPass(m_passIndex);
        const ResourceRecord* record = m_executor->ValidateHandle(
            ResourceKind::Texture,
            handle.index,
            handle.graphId,
            m_passIndex,
            pass.name,
            "GetTexture");
        if (record == nullptr)
        {
            return nullptr;
        }

        const ResourceAccess* declared = FindDeclaredAccess(m_accesses, ResourceKind::Texture, handle.index);
        if (declared == nullptr)
        {
            m_executor->AddError(
                ErrorCategory::UndeclaredAccess,
                std::format(
                    "GetTexture of '{}' on pass '{}' is not in this pass's use list",
                    record->name,
                    pass.name),
                m_passIndex,
                pass.name,
                record->name);
            return nullptr;
        }

        const PhysicalTexture* physical = m_executor->m_registry.GetTexture(handle.index);
        if (physical == nullptr)
        {
            m_executor->AddError(
                ErrorCategory::UnregisteredImport,
                std::format("GetTexture of unregistered resource '{}' on pass '{}'", record->name, pass.name),
                m_passIndex,
                pass.name,
                record->name);
            return nullptr;
        }
        return physical;
    }

    const PhysicalBuffer* PassContext::GetBuffer(BufferHandle handle)
    {
        if (!m_active)
        {
            m_executor->AddError(
                ErrorCategory::ExpiredContext,
                "GetBuffer after the pass has ended",
                m_passIndex,
                m_executor->m_builder->GetPass(m_passIndex).name,
                "");
            return nullptr;
        }

        const PassRecord& pass = m_executor->m_builder->GetPass(m_passIndex);
        const ResourceRecord* record = m_executor->ValidateHandle(
            ResourceKind::Buffer,
            handle.index,
            handle.graphId,
            m_passIndex,
            pass.name,
            "GetBuffer");
        if (record == nullptr)
        {
            return nullptr;
        }

        const ResourceAccess* declared = FindDeclaredAccess(m_accesses, ResourceKind::Buffer, handle.index);
        if (declared == nullptr)
        {
            m_executor->AddError(
                ErrorCategory::UndeclaredAccess,
                std::format(
                    "GetBuffer of '{}' on pass '{}' is not in this pass's use list",
                    record->name,
                    pass.name),
                m_passIndex,
                pass.name,
                record->name);
            return nullptr;
        }

        const PhysicalBuffer* physical = m_executor->m_registry.GetBuffer(handle.index);
        if (physical == nullptr)
        {
            m_executor->AddError(
                ErrorCategory::UnregisteredImport,
                std::format("GetBuffer of unregistered resource '{}' on pass '{}'", record->name, pass.name),
                m_passIndex,
                pass.name,
                record->name);
            return nullptr;
        }
        return physical;
    }
}
