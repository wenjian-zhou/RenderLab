#include "GraphBuilder.h"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <format>
#include <utility>

namespace renderlab::rdg
{
    namespace
    {
        // 0 is reserved so a default-constructed handle (graphId 0) can
        // never resolve in a live graph.
        std::atomic<uint32_t> g_nextGraphId{1u};

        constexpr uint32_t kKnownPassFlagBits =
            static_cast<uint32_t>(PassFlags::Raster) | static_cast<uint32_t>(PassFlags::NeverCull);

        const char* ToString(ResourceKind kind)
        {
            return kind == ResourceKind::Texture ? "texture" : "buffer";
        }
    }

    PassBuilder::PassBuilder(GraphBuilder& builder, uint32_t passIndex)
        : m_builder(&builder)
        , m_passIndex(passIndex)
    {
    }

    void PassBuilder::Use(TextureHandle handle, Access access)
    {
        if (m_builder != nullptr)
        {
            m_builder->DeclareUse(m_passIndex, handle, access);
        }
    }

    void PassBuilder::Use(BufferHandle handle, Access access)
    {
        if (m_builder != nullptr)
        {
            m_builder->DeclareUse(m_passIndex, handle, access);
        }
    }

    GraphBuilder::GraphBuilder()
        : m_graphId(g_nextGraphId.fetch_add(1u, std::memory_order_relaxed))
    {
    }

    TextureHandle GraphBuilder::CreateTexture(const TextureDesc& desc)
    {
        return CreateTextureResource(desc, false);
    }

    TextureHandle GraphBuilder::ImportTexture(const TextureDesc& desc)
    {
        return CreateTextureResource(desc, true);
    }

    BufferHandle GraphBuilder::CreateBuffer(const BufferDesc& desc)
    {
        return CreateBufferResource(desc, false);
    }

    BufferHandle GraphBuilder::ImportBuffer(const BufferDesc& desc)
    {
        return CreateBufferResource(desc, true);
    }

    TextureHandle GraphBuilder::CreateTextureResource(const TextureDesc& desc, bool imported)
    {
        if (!ValidateTextureDesc(desc))
        {
            return TextureHandle{};
        }
        const uint32_t index = static_cast<uint32_t>(m_resources.size());
        m_resources.push_back(ResourceRecord{
            desc.name,
            ResourceKind::Texture,
            desc,
            imported,
            false,
            false,
            Access::Unknown,
            false,
            Access::Unknown});
        return TextureHandle{index, m_graphId};
    }

    BufferHandle GraphBuilder::CreateBufferResource(const BufferDesc& desc, bool imported)
    {
        if (!ValidateBufferDesc(desc))
        {
            return BufferHandle{};
        }
        const uint32_t index = static_cast<uint32_t>(m_resources.size());
        m_resources.push_back(ResourceRecord{
            desc.name,
            ResourceKind::Buffer,
            desc,
            imported,
            false,
            false,
            Access::Unknown,
            false,
            Access::Unknown});
        return BufferHandle{index, m_graphId};
    }

    bool GraphBuilder::ValidateTextureDesc(const TextureDesc& desc)
    {
        bool valid = true;
        if (desc.name.empty())
        {
            AddError(
                ErrorCategory::InvalidName,
                "texture descriptor name must not be empty",
                Error::kNoPass,
                "",
                "");
            valid = false;
        }
        if (desc.width == 0 || desc.height == 0)
        {
            AddError(
                ErrorCategory::InvalidDescriptor,
                std::format(
                    "texture '{}' has width {} and height {}; both must be nonzero",
                    desc.name,
                    desc.width,
                    desc.height),
                Error::kNoPass,
                "",
                desc.name);
            valid = false;
        }
        if (desc.format == Format::Unknown)
        {
            AddError(
                ErrorCategory::InvalidDescriptor,
                std::format("texture '{}' has Format::Unknown", desc.name),
                Error::kNoPass,
                "",
                desc.name);
            valid = false;
        }
        return valid;
    }

    bool GraphBuilder::ValidateBufferDesc(const BufferDesc& desc)
    {
        bool valid = true;
        if (desc.name.empty())
        {
            AddError(
                ErrorCategory::InvalidName,
                "buffer descriptor name must not be empty",
                Error::kNoPass,
                "",
                "");
            valid = false;
        }
        if (desc.bytesPerElement == 0)
        {
            AddError(
                ErrorCategory::InvalidDescriptor,
                std::format("buffer '{}' has bytesPerElement 0", desc.name),
                Error::kNoPass,
                "",
                desc.name);
            valid = false;
        }
        if (desc.numElements == 0)
        {
            AddError(
                ErrorCategory::InvalidDescriptor,
                std::format("buffer '{}' has numElements 0", desc.name),
                Error::kNoPass,
                "",
                desc.name);
            valid = false;
        }
        return valid;
    }

    PassBuilder GraphBuilder::AddPass(std::string_view name, PassFlags flags)
    {
        bool valid = true;
        if (name.empty())
        {
            AddError(ErrorCategory::InvalidName, "pass name must not be empty", Error::kNoPass, "", "");
            valid = false;
        }
        const uint32_t unknownBits = static_cast<uint32_t>(flags) & ~kKnownPassFlagBits;
        if (unknownBits != 0)
        {
            AddError(
                ErrorCategory::InvalidPassFlags,
                std::format("pass '{}' has unknown flag bits 0x{:X}", name, unknownBits),
                Error::kNoPass,
                std::string(name),
                "");
            valid = false;
        }
        if (!valid)
        {
            return PassBuilder{};
        }
        const uint32_t index = static_cast<uint32_t>(m_passes.size());
        m_passes.push_back(PassRecord{std::string(name), flags, {}});
        return PassBuilder{*this, index};
    }

    PassBuilder GraphBuilder::AddPass(std::string_view name, PassFlags flags, PassLambda lambda)
    {
        const size_t countBefore = GetPassCount();
        PassBuilder pass = AddPass(name, flags);
        if (!pass.IsValid() || GetPassCount() == countBefore)
        {
            return pass;
        }
        if (m_lambdas.size() < GetPassCount())
        {
            m_lambdas.resize(GetPassCount());
        }
        m_lambdas[pass.PassIndex()] = std::move(lambda);
        return pass;
    }

    const PassLambda* GraphBuilder::FindLambda(uint32_t passIndex) const
    {
        if (passIndex >= m_lambdas.size())
        {
            return nullptr;
        }
        if (!m_lambdas[passIndex])
        {
            return nullptr;
        }
        return &m_lambdas[passIndex];
    }

    bool GraphBuilder::DeclareUse(uint32_t passIndex, TextureHandle handle, Access access)
    {
        return DeclareUseInternal(passIndex, ResourceKind::Texture, handle.index, handle.graphId, access);
    }

    bool GraphBuilder::DeclareUse(uint32_t passIndex, BufferHandle handle, Access access)
    {
        return DeclareUseInternal(passIndex, ResourceKind::Buffer, handle.index, handle.graphId, access);
    }

    bool GraphBuilder::DeclareUseInternal(
        uint32_t passIndex,
        ResourceKind kind,
        uint32_t index,
        uint32_t graphId,
        Access access)
    {
        assert(passIndex < m_passes.size());
        const ResourceRecord* record =
            ResolveHandle(kind, index, graphId, passIndex, m_passes[passIndex].name, "use declaration");
        if (record == nullptr)
        {
            return false;
        }
        if (!IsPassUseAccess(access))
        {
            AddError(
                ErrorCategory::IncompatibleAccess,
                std::format(
                    "use of resource '{}' in pass '{}' has access {} which is not a single pass-use bit",
                    record->name,
                    m_passes[passIndex].name,
                    ToString(access)),
                passIndex,
                m_passes[passIndex].name,
                record->name);
            return false;
        }
        for (const ResourceAccess& existing : m_passes[passIndex].accesses)
        {
            if (existing.index != index)
            {
                continue;
            }
            if (!IsWritableAccess(existing.access) && !IsWritableAccess(access))
            {
                continue;
            }
            AddError(
                existing.access == access ? ErrorCategory::DuplicateWrite : ErrorCategory::IncompatibleAccess,
                std::format(
                    "conflicting use of resource '{}' as {} in pass '{}'",
                    record->name,
                    ToString(access),
                    m_passes[passIndex].name),
                passIndex,
                m_passes[passIndex].name,
                record->name);
            return false;
        }
        if (access == Access::ShaderResource && !record->imported)
        {
            bool produced = false;
            for (uint32_t earlier = 0; earlier < passIndex; ++earlier)
            {
                for (const ResourceAccess& earlierUse : m_passes[earlier].accesses)
                {
                    if (earlierUse.index == index && IsWritableAccess(earlierUse.access))
                    {
                        produced = true;
                        break;
                    }
                }
                if (produced)
                {
                    break;
                }
            }
            if (!produced)
            {
                AddError(
                    ErrorCategory::ReadBeforeProduce,
                    std::format("read of unproduced resource '{}'", record->name),
                    passIndex,
                    m_passes[passIndex].name,
                    record->name);
                return false;
            }
        }
        m_passes[passIndex].accesses.push_back(ResourceAccess{kind, index, access});
        return true;
    }

    void GraphBuilder::SetInitialAccess(TextureHandle handle, Access access)
    {
        SetInitialAccessInternal(ResourceKind::Texture, handle.index, handle.graphId, access);
    }

    void GraphBuilder::SetInitialAccess(BufferHandle handle, Access access)
    {
        SetInitialAccessInternal(ResourceKind::Buffer, handle.index, handle.graphId, access);
    }

    void GraphBuilder::SetInitialAccessInternal(
        ResourceKind kind,
        uint32_t index,
        uint32_t graphId,
        Access access)
    {
        const ResourceRecord* record =
            ResolveHandle(kind, index, graphId, Error::kNoPass, "", "initial access");
        if (record == nullptr)
        {
            return;
        }
        if (!IsBoundaryAccess(access))
        {
            AddError(
                ErrorCategory::IncompatibleAccess,
                std::format(
                    "initial access {} of resource '{}' is not a single boundary bit",
                    ToString(access),
                    record->name),
                Error::kNoPass,
                "",
                record->name);
            return;
        }
        if (!record->imported)
        {
            AddError(
                ErrorCategory::IncompatibleAccess,
                std::format("initial access of created resource '{}'", record->name),
                Error::kNoPass,
                "",
                record->name);
            return;
        }
        ResourceRecord& resource = m_resources[index];
        if (resource.hasInitialAccess && resource.initialAccess != access)
        {
            AddError(
                ErrorCategory::IncompatibleAccess,
                std::format(
                    "initial access of resource '{}' is already {}",
                    record->name,
                    ToString(resource.initialAccess)),
                Error::kNoPass,
                "",
                record->name);
            return;
        }
        resource.hasInitialAccess = true;
        resource.initialAccess = access;
    }

    void GraphBuilder::ExportTexture(TextureHandle handle, Access finalAccess)
    {
        ExportResource(ResourceKind::Texture, handle.index, handle.graphId, finalAccess);
    }

    void GraphBuilder::ExportBuffer(BufferHandle handle, Access finalAccess)
    {
        ExportResource(ResourceKind::Buffer, handle.index, handle.graphId, finalAccess);
    }

    bool GraphBuilder::HasWritableUse(uint32_t index) const
    {
        for (const PassRecord& pass : m_passes)
        {
            for (const ResourceAccess& use : pass.accesses)
            {
                if (use.index == index && IsWritableAccess(use.access))
                {
                    return true;
                }
            }
        }
        return false;
    }

    void GraphBuilder::ExportResource(
        ResourceKind kind,
        uint32_t index,
        uint32_t graphId,
        Access finalAccess)
    {
        const ResourceRecord* record =
            ResolveHandle(kind, index, graphId, Error::kNoPass, "", "export declaration");
        if (record == nullptr)
        {
            return;
        }
        if (!IsBoundaryAccess(finalAccess))
        {
            AddError(
                ErrorCategory::IncompatibleAccess,
                std::format(
                    "export access {} of resource '{}' is not a single boundary bit",
                    ToString(finalAccess),
                    record->name),
                Error::kNoPass,
                "",
                record->name);
            return;
        }
        if (!record->imported && !HasWritableUse(index))
        {
            AddError(
                ErrorCategory::ReadBeforeProduce,
                std::format("export of unproduced resource '{}'", record->name),
                Error::kNoPass,
                "",
                record->name);
            return;
        }
        ResourceRecord& resource = m_resources[index];
        if (resource.hasFinalAccess && resource.finalAccess != finalAccess)
        {
            AddError(
                ErrorCategory::IncompatibleAccess,
                std::format(
                    "export access of resource '{}' is already {}",
                    record->name,
                    ToString(resource.finalAccess)),
                Error::kNoPass,
                "",
                record->name);
            return;
        }
        resource.exported = true;
        resource.hasFinalAccess = true;
        resource.finalAccess = finalAccess;
    }

    const ResourceRecord* GraphBuilder::ResolveHandle(
        ResourceKind kind,
        uint32_t index,
        uint32_t graphId,
        uint32_t passIndex,
        const std::string& passName,
        const char* operation)
    {
        // Deterministic check order: when a forged handle violates several
        // rules at once, the earliest category wins.
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
        if (graphId != m_graphId)
        {
            AddError(
                ErrorCategory::ForeignGraph,
                std::format(
                    "{} handle (index {}) in {} belongs to graph {}, not this graph {}",
                    ToString(kind),
                    index,
                    context,
                    graphId,
                    m_graphId),
                passIndex,
                passName,
                "");
            return nullptr;
        }
        if (index >= m_resources.size())
        {
            AddError(
                ErrorCategory::IncompatibleAccess,
                std::format(
                    "{} handle index {} in {} is out of range (this graph has {} resources)",
                    ToString(kind),
                    index,
                    context,
                    m_resources.size()),
                passIndex,
                passName,
                "");
            return nullptr;
        }
        const ResourceRecord& record = m_resources[index];
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
        return &record;
    }

    void GraphBuilder::AssertNoErrors() const
    {
        assert(m_errors.empty() && "GraphBuilder recorded RDG declaration errors");
    }

    const PassRecord& GraphBuilder::GetPass(uint32_t passIndex) const
    {
        assert(passIndex < m_passes.size());
        return m_passes[passIndex];
    }

    const ResourceRecord& GraphBuilder::GetResource(uint32_t resourceIndex) const
    {
        assert(resourceIndex < m_resources.size());
        return m_resources[resourceIndex];
    }

    void GraphBuilder::AddError(
        ErrorCategory category,
        std::string message,
        uint32_t passIndex,
        std::string passName,
        std::string resourceName)
    {
        m_errors.push_back(
            Error{category, std::move(message), passIndex, std::move(passName), std::move(resourceName)});
    }
}
