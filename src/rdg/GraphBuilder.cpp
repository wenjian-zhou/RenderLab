#include "GraphBuilder.h"

#include <atomic>
#include <cassert>
#include <format>

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

        const char* ToString(AccessMode mode)
        {
            return mode == AccessMode::Read ? "read declaration" : "write declaration";
        }
    }

    PassBuilder::PassBuilder(GraphBuilder& builder, uint32_t passIndex)
        : m_builder(&builder)
        , m_passIndex(passIndex)
    {
    }

    void PassBuilder::Read(TextureHandle handle)
    {
        if (m_builder != nullptr)
        {
            m_builder->DeclareAccess(m_passIndex, handle, AccessMode::Read);
        }
    }

    void PassBuilder::Read(BufferHandle handle)
    {
        if (m_builder != nullptr)
        {
            m_builder->DeclareAccess(m_passIndex, handle, AccessMode::Read);
        }
    }

    void PassBuilder::Write(TextureHandle handle)
    {
        if (m_builder != nullptr)
        {
            m_builder->DeclareAccess(m_passIndex, handle, AccessMode::Write);
        }
    }

    void PassBuilder::Write(BufferHandle handle)
    {
        if (m_builder != nullptr)
        {
            m_builder->DeclareAccess(m_passIndex, handle, AccessMode::Write);
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
        m_resources.push_back(
            ResourceRecord{desc.name, ResourceKind::Texture, desc, imported, false, 0u});
        return TextureHandle{index, 0u, m_graphId};
    }

    BufferHandle GraphBuilder::CreateBufferResource(const BufferDesc& desc, bool imported)
    {
        if (!ValidateBufferDesc(desc))
        {
            return BufferHandle{};
        }
        const uint32_t index = static_cast<uint32_t>(m_resources.size());
        m_resources.push_back(
            ResourceRecord{desc.name, ResourceKind::Buffer, desc, imported, false, 0u});
        return BufferHandle{index, 0u, m_graphId};
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

    void GraphBuilder::DeclareAccess(uint32_t passIndex, TextureHandle handle, AccessMode mode)
    {
        DeclareAccessInternal(
            passIndex, ResourceKind::Texture, handle.index, handle.version, handle.graphId, mode);
    }

    void GraphBuilder::DeclareAccess(uint32_t passIndex, BufferHandle handle, AccessMode mode)
    {
        DeclareAccessInternal(
            passIndex, ResourceKind::Buffer, handle.index, handle.version, handle.graphId, mode);
    }

    void GraphBuilder::DeclareAccessInternal(
        uint32_t passIndex,
        ResourceKind kind,
        uint32_t index,
        uint32_t version,
        uint32_t graphId,
        AccessMode mode)
    {
        assert(passIndex < m_passes.size());
        const ResourceRecord* record = ResolveHandle(
            kind, index, version, graphId, passIndex, m_passes[passIndex].name, ToString(mode));
        if (record == nullptr)
        {
            return; // rejected; ResolveHandle recorded the error
        }
        m_passes[passIndex].accesses.push_back(ResourceAccess{kind, index, version, mode});
    }

    void GraphBuilder::ExportTexture(TextureHandle handle)
    {
        ExportResource(ResourceKind::Texture, handle.index, handle.version, handle.graphId);
    }

    void GraphBuilder::ExportBuffer(BufferHandle handle)
    {
        ExportResource(ResourceKind::Buffer, handle.index, handle.version, handle.graphId);
    }

    void GraphBuilder::ExportResource(ResourceKind kind, uint32_t index, uint32_t version, uint32_t graphId)
    {
        const ResourceRecord* record =
            ResolveHandle(kind, index, version, graphId, Error::kNoPass, "", "export declaration");
        if (record != nullptr)
        {
            m_resources[index].exported = true;
        }
    }

    const ResourceRecord* GraphBuilder::ResolveHandle(
        ResourceKind kind,
        uint32_t index,
        uint32_t version,
        uint32_t graphId,
        uint32_t passIndex,
        const std::string& passName,
        const char* operation)
    {
        // Deterministic check order: when a forged handle violates several
        // rules at once, the earliest category wins (docs/rdg.md).
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
                    "{} handle (index {}, version {}) in {} belongs to graph {}, not this graph {}",
                    ToString(kind),
                    index,
                    version,
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
                ErrorCategory::StaleVersion,
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
        if (version != record.currentVersion)
        {
            AddError(
                ErrorCategory::StaleVersion,
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
