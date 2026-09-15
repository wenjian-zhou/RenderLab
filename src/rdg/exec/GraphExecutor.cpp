#include "GraphExecutor.h"

#include <cassert>
#include <format>
#include <utility>

namespace renderlab::rdg::exec
{
    namespace
    {
        const char* ToString(ResourceKind kind)
        {
            return kind == ResourceKind::Texture ? "texture" : "buffer";
        }
    }

    GraphExecutor::GraphExecutor(const GraphBuilder& builder, const CompileResult& result)
        : m_builder(&builder)
        , m_result(&result)
    {
        m_registry.Reset(builder.GetResourceCount());
    }

    void GraphExecutor::RegisterImport(TextureHandle handle, PhysicalTexture physical)
    {
        const ResourceRecord* record = ValidateHandle(
            ResourceKind::Texture,
            handle.index,
            handle.version,
            handle.graphId,
            Error::kNoPass,
            "",
            "import bind",
            VersionRule::AnyInRange);
        if (record == nullptr)
        {
            return;
        }
        if (!record->imported || m_registry.HasTexture(handle.index))
        {
            AddError(
                ErrorCategory::IncompatibleAccess,
                std::format(
                    "cannot bind physical texture '{}' (index {}): {}",
                    record->name,
                    handle.index,
                    record->imported ? "already registered" : "resource is not imported"),
                Error::kNoPass,
                "",
                record->name);
            return;
        }
        m_registry.SetTexture(handle.index, std::move(physical));
    }

    void GraphExecutor::RegisterImport(BufferHandle handle, PhysicalBuffer physical)
    {
        const ResourceRecord* record = ValidateHandle(
            ResourceKind::Buffer,
            handle.index,
            handle.version,
            handle.graphId,
            Error::kNoPass,
            "",
            "import bind",
            VersionRule::AnyInRange);
        if (record == nullptr)
        {
            return;
        }
        if (!record->imported || m_registry.HasBuffer(handle.index))
        {
            AddError(
                ErrorCategory::IncompatibleAccess,
                std::format(
                    "cannot bind physical buffer '{}' (index {}): {}",
                    record->name,
                    handle.index,
                    record->imported ? "already registered" : "resource is not imported"),
                Error::kNoPass,
                "",
                record->name);
            return;
        }
        m_registry.SetBuffer(handle.index, std::move(physical));
    }

    const PhysicalTexture* GraphExecutor::GetExported(TextureHandle handle)
    {
        const ResourceRecord* record = ValidateHandle(
            ResourceKind::Texture,
            handle.index,
            handle.version,
            handle.graphId,
            Error::kNoPass,
            "",
            "export lookup",
            VersionRule::Current);
        if (record == nullptr)
        {
            return nullptr;
        }
        if (!record->exported)
        {
            AddError(
                ErrorCategory::IncompatibleAccess,
                std::format("export lookup of resource '{}' which is not exported", record->name),
                Error::kNoPass,
                "",
                record->name);
            return nullptr;
        }
        const PhysicalTexture* physical = m_registry.GetTexture(handle.index);
        if (physical == nullptr)
        {
            AddError(
                ErrorCategory::UnregisteredImport,
                std::format("export lookup of unregistered imported texture '{}'", record->name),
                Error::kNoPass,
                "",
                record->name);
            return nullptr;
        }
        return physical;
    }

    const PhysicalBuffer* GraphExecutor::GetExported(BufferHandle handle)
    {
        const ResourceRecord* record = ValidateHandle(
            ResourceKind::Buffer,
            handle.index,
            handle.version,
            handle.graphId,
            Error::kNoPass,
            "",
            "export lookup",
            VersionRule::Current);
        if (record == nullptr)
        {
            return nullptr;
        }
        if (!record->exported)
        {
            AddError(
                ErrorCategory::IncompatibleAccess,
                std::format("export lookup of resource '{}' which is not exported", record->name),
                Error::kNoPass,
                "",
                record->name);
            return nullptr;
        }
        const PhysicalBuffer* physical = m_registry.GetBuffer(handle.index);
        if (physical == nullptr)
        {
            AddError(
                ErrorCategory::UnregisteredImport,
                std::format("export lookup of unregistered imported buffer '{}'", record->name),
                Error::kNoPass,
                "",
                record->name);
            return nullptr;
        }
        return physical;
    }

    void GraphExecutor::ExecutePass(uint32_t passIndex, const std::function<void(PassContext&)>& callback)
    {
        if (!m_result->IsSuccess() || passIndex >= m_builder->GetPassCount())
        {
            AddError(
                ErrorCategory::InvalidPass,
                std::format("ExecutePass({}) is not a live compiled pass", passIndex),
                passIndex >= m_builder->GetPassCount() ? Error::kNoPass : passIndex,
                passIndex < m_builder->GetPassCount() ? m_builder->GetPass(passIndex).name : "",
                "");
            return;
        }

        const std::span<const PassCullState> cullStates = m_result->GetPassCullStates();
        if (passIndex >= cullStates.size() || cullStates[passIndex].culled)
        {
            const std::string& passName = m_builder->GetPass(passIndex).name;
            AddError(
                ErrorCategory::InvalidPass,
                std::format("ExecutePass({}) pass '{}' is culled or has no cull state", passIndex, passName),
                passIndex,
                passName,
                "");
            return;
        }

        m_context.Activate(*this, passIndex, m_builder->GetPass(passIndex).accesses);
        callback(m_context);
        m_context.Deactivate();
    }

    void GraphExecutor::AssertNoErrors() const
    {
        assert(m_errors.empty() && "GraphExecutor recorded RDG execution errors");
    }

    const ResourceRecord* GraphExecutor::ValidateHandle(
        ResourceKind kind,
        uint32_t index,
        uint32_t version,
        uint32_t graphId,
        uint32_t passIndex,
        const std::string& passName,
        const char* operation,
        VersionRule versionRule)
    {
        const std::string context =
            passName.empty() ? std::string(operation) : std::format("{} on pass '{}'", operation, passName);
        if (index == kNullHandleIndex)
        {
            AddError(
                ErrorCategory::NullHandle,
                std::format("null {} handle in {}", ToString(kind), context),
                passIndex,
                passName,
                "");
            return nullptr;
        }
        if (graphId != m_builder->GetGraphId())
        {
            AddError(
                ErrorCategory::ForeignGraph,
                std::format(
                    "{} handle (index {}, version {}) in {} belongs to graph {}, not this graph {}",
                    ToString(kind),
                    index,
                    version,
                    context,
                    graphId,
                    m_builder->GetGraphId()),
                passIndex,
                passName,
                "");
            return nullptr;
        }
        if (index >= m_builder->GetResourceCount())
        {
            AddError(
                ErrorCategory::StaleVersion,
                std::format(
                    "{} handle index {} in {} is out of range (this graph has {} resources)",
                    ToString(kind),
                    index,
                    context,
                    m_builder->GetResourceCount()),
                passIndex,
                passName,
                "");
            return nullptr;
        }
        const ResourceRecord& record = m_builder->GetResource(index);
        if (record.kind != kind)
        {
            AddError(
                ErrorCategory::TypeMismatch,
                std::format(
                    "{} handle in {} targets resource '{}' (index {}), which is a {}",
                    ToString(kind),
                    context,
                    record.name,
                    index,
                    ToString(record.kind)),
                passIndex,
                passName,
                record.name);
            return nullptr;
        }
        if (versionRule == VersionRule::Current)
        {
            if (version != record.currentVersion)
            {
                AddError(
                    version < record.currentVersion ? ErrorCategory::SupersededUse : ErrorCategory::StaleVersion,
                    std::format(
                        "{} handle version {} in {} does not match current version {} of resource '{}'",
                        ToString(kind),
                        version,
                        context,
                        record.currentVersion,
                        record.name),
                    passIndex,
                    passName,
                    record.name);
                return nullptr;
            }
        }
        else if (version >= record.versions.size())
        {
            AddError(
                ErrorCategory::StaleVersion,
                std::format(
                    "{} handle version {} in {} is out of range for resource '{}' ({} versions)",
                    ToString(kind),
                    version,
                    context,
                    record.name,
                    record.versions.size()),
                passIndex,
                passName,
                record.name);
            return nullptr;
        }
        return &record;
    }

    void GraphExecutor::AddError(
        ErrorCategory category,
        std::string message,
        uint32_t passIndex,
        std::string passName,
        std::string resourceName)
    {
        m_errors.push_back(Error{
            category,
            std::move(message),
            passIndex,
            std::move(passName),
            std::move(resourceName)});
    }
}
