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

    void PassBuilder::read(TextureHandle handle)
    {
        if (m_builder != nullptr)
        {
            m_builder->declareAccess(m_passIndex, handle, AccessMode::Read);
        }
    }

    void PassBuilder::read(BufferHandle handle)
    {
        if (m_builder != nullptr)
        {
            m_builder->declareAccess(m_passIndex, handle, AccessMode::Read);
        }
    }

    void PassBuilder::write(TextureHandle handle)
    {
        if (m_builder != nullptr)
        {
            m_builder->declareAccess(m_passIndex, handle, AccessMode::Write);
        }
    }

    void PassBuilder::write(BufferHandle handle)
    {
        if (m_builder != nullptr)
        {
            m_builder->declareAccess(m_passIndex, handle, AccessMode::Write);
        }
    }

    GraphBuilder::GraphBuilder()
        : m_graphId(g_nextGraphId.fetch_add(1u, std::memory_order_relaxed))
    {
    }

    TextureHandle GraphBuilder::createTexture(const TextureDesc& desc)
    {
        return createTextureResource(desc, false);
    }

    TextureHandle GraphBuilder::importTexture(const TextureDesc& desc)
    {
        return createTextureResource(desc, true);
    }

    BufferHandle GraphBuilder::createBuffer(const BufferDesc& desc)
    {
        return createBufferResource(desc, false);
    }

    BufferHandle GraphBuilder::importBuffer(const BufferDesc& desc)
    {
        return createBufferResource(desc, true);
    }

    TextureHandle GraphBuilder::createTextureResource(const TextureDesc& desc, bool imported)
    {
        if (!validateTextureDesc(desc))
        {
            return TextureHandle{};
        }
        const uint32_t index = static_cast<uint32_t>(m_resources.size());
        m_resources.push_back(
            ResourceRecord{desc.name, ResourceKind::Texture, desc, imported, false, 0u});
        return TextureHandle{index, 0u, m_graphId};
    }

    BufferHandle GraphBuilder::createBufferResource(const BufferDesc& desc, bool imported)
    {
        if (!validateBufferDesc(desc))
        {
            return BufferHandle{};
        }
        const uint32_t index = static_cast<uint32_t>(m_resources.size());
        m_resources.push_back(
            ResourceRecord{desc.name, ResourceKind::Buffer, desc, imported, false, 0u});
        return BufferHandle{index, 0u, m_graphId};
    }

    bool GraphBuilder::validateTextureDesc(const TextureDesc& desc)
    {
        bool valid = true;
        if (desc.name.empty())
        {
            addError(
                ErrorCategory::InvalidName,
                "texture descriptor name must not be empty",
                Error::kNoPass,
                "",
                "");
            valid = false;
        }
        if (desc.width == 0 || desc.height == 0)
        {
            addError(
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
            addError(
                ErrorCategory::InvalidDescriptor,
                std::format("texture '{}' has Format::Unknown", desc.name),
                Error::kNoPass,
                "",
                desc.name);
            valid = false;
        }
        return valid;
    }

    bool GraphBuilder::validateBufferDesc(const BufferDesc& desc)
    {
        bool valid = true;
        if (desc.name.empty())
        {
            addError(
                ErrorCategory::InvalidName,
                "buffer descriptor name must not be empty",
                Error::kNoPass,
                "",
                "");
            valid = false;
        }
        if (desc.bytesPerElement == 0)
        {
            addError(
                ErrorCategory::InvalidDescriptor,
                std::format("buffer '{}' has bytesPerElement 0", desc.name),
                Error::kNoPass,
                "",
                desc.name);
            valid = false;
        }
        if (desc.numElements == 0)
        {
            addError(
                ErrorCategory::InvalidDescriptor,
                std::format("buffer '{}' has numElements 0", desc.name),
                Error::kNoPass,
                "",
                desc.name);
            valid = false;
        }
        return valid;
    }

    PassBuilder GraphBuilder::addPass(std::string_view name, PassFlags flags)
    {
        bool valid = true;
        if (name.empty())
        {
            addError(ErrorCategory::InvalidName, "pass name must not be empty", Error::kNoPass, "", "");
            valid = false;
        }
        const uint32_t unknownBits = static_cast<uint32_t>(flags) & ~kKnownPassFlagBits;
        if (unknownBits != 0)
        {
            addError(
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

    void GraphBuilder::declareAccess(uint32_t passIndex, TextureHandle handle, AccessMode mode)
    {
        declareAccessInternal(
            passIndex, ResourceKind::Texture, handle.index, handle.version, handle.graphId, mode);
    }

    void GraphBuilder::declareAccess(uint32_t passIndex, BufferHandle handle, AccessMode mode)
    {
        declareAccessInternal(
            passIndex, ResourceKind::Buffer, handle.index, handle.version, handle.graphId, mode);
    }

    void GraphBuilder::declareAccessInternal(
        uint32_t passIndex,
        ResourceKind kind,
        uint32_t index,
        uint32_t version,
        uint32_t graphId,
        AccessMode mode)
    {
        assert(passIndex < m_passes.size());
        const ResourceRecord* record = resolveHandle(
            kind, index, version, graphId, passIndex, m_passes[passIndex].name, ToString(mode));
        if (record == nullptr)
        {
            return; // rejected; resolveHandle recorded the error
        }
        m_passes[passIndex].accesses.push_back(ResourceAccess{kind, index, version, mode});
    }

    void GraphBuilder::exportTexture(TextureHandle handle)
    {
        exportResource(ResourceKind::Texture, handle.index, handle.version, handle.graphId);
    }

    void GraphBuilder::exportBuffer(BufferHandle handle)
    {
        exportResource(ResourceKind::Buffer, handle.index, handle.version, handle.graphId);
    }

    void GraphBuilder::exportResource(ResourceKind kind, uint32_t index, uint32_t version, uint32_t graphId)
    {
        const ResourceRecord* record =
            resolveHandle(kind, index, version, graphId, Error::kNoPass, "", "export declaration");
        if (record != nullptr)
        {
            m_resources[index].exported = true;
        }
    }

    const ResourceRecord* GraphBuilder::resolveHandle(
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
            addError(
                ErrorCategory::NullHandle,
                std::format("null {} handle in {}", ToString(kind), context),
                passIndex,
                passName,
                "");
            return nullptr;
        }
        if (graphId != m_graphId)
        {
            addError(
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
            addError(
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
            addError(
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
            addError(
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

    void GraphBuilder::assertNoErrors() const
    {
        assert(m_errors.empty() && "GraphBuilder recorded RDG declaration errors");
    }

    const PassRecord& GraphBuilder::pass(uint32_t passIndex) const
    {
        assert(passIndex < m_passes.size());
        return m_passes[passIndex];
    }

    const ResourceRecord& GraphBuilder::resource(uint32_t resourceIndex) const
    {
        assert(resourceIndex < m_resources.size());
        return m_resources[resourceIndex];
    }

    void GraphBuilder::addError(
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
