#include "GraphExecutor.h"
#include "AccessMap.h"
#include "FormatMap.h"
#include "TransientReuse.h"

#include <nvrhi/nvrhi.h>

#include <cassert>
#include <format>
#include <utility>
#include <variant>

namespace renderlab::rdg
{
    namespace
    {
        const char* ToString(ResourceKind kind)
        {
            return kind == ResourceKind::Texture ? "texture" : "buffer";
        }
    }

    struct GraphExecutor::GpuStorage
    {
        std::vector<nvrhi::TextureHandle> textures;
        std::vector<nvrhi::BufferHandle> buffers;
    };

    GraphExecutor::GraphExecutor(const GraphBuilder& builder, const CompileResult& result)
        : m_builder(&builder)
        , m_compileSuccess(result.IsSuccess())
        , m_cullStates(result.GetPassCullStates().begin(), result.GetPassCullStates().end())
        , m_livePassOrder(result.GetLivePassOrder().begin(), result.GetLivePassOrder().end())
        , m_resourceLifetimes(result.GetResourceLifetimes().begin(), result.GetResourceLifetimes().end())
    {
        m_registry.Reset(builder.GetResourceCount());
    }

    GraphExecutor::~GraphExecutor() = default;

    void GraphExecutor::SetDevice(nvrhi::IDevice* device)
    {
        if (m_allocated)
        {
            AddError(
                ErrorCategory::IncompatibleAccess,
                "SetDevice after Allocate()",
                Error::kNoPass,
                "",
                "");
            return;
        }
        m_device = device;
    }

    void GraphExecutor::SetTransientReuse(bool enabled)
    {
        if (m_allocated)
        {
            AddError(
                ErrorCategory::IncompatibleAccess,
                "SetTransientReuse after Allocate()",
                Error::kNoPass,
                "",
                "");
            return;
        }
        m_transientReuse = enabled;
    }

    void GraphExecutor::Plan()
    {
        if (!m_compileSuccess)
        {
            AddError(
                ErrorCategory::InvalidPass,
                "Plan() requires a successful compile",
                Error::kNoPass,
                "",
                "");
            return;
        }
        if (m_planned)
        {
            AddError(
                ErrorCategory::IncompatibleAccess,
                "Plan() already ran",
                Error::kNoPass,
                "",
                "");
            return;
        }

        AccessPlan plan;
        const size_t resourceCount = m_builder->GetResourceCount();
        std::vector<Access> current(resourceCount, Access::Unknown);
        const size_t errorCountBefore = m_errors.size();

        for (uint32_t index = 0; index < resourceCount; ++index)
        {
            const ResourceRecord& record = m_builder->GetResource(index);
            Access initial = Access::ShaderResource;
            if (record.kind == ResourceKind::Texture)
            {
                if (record.imported && record.exported)
                {
                    initial = Access::Present;
                }
                else
                {
                    const Format format = std::get<TextureDesc>(record.desc).format;
                    initial = format == Format::D32Float ? Access::DepthWrite : Access::RenderTarget;
                }
            }
            plan.resources.push_back(ResourceBoundary{
                index,
                record.name,
                initial,
                initial,
                record.imported,
                record.exported});
            current[index] = initial;
        }

        for (const uint32_t passIndex : m_livePassOrder)
        {
            if (passIndex >= m_cullStates.size() || m_cullStates[passIndex].culled)
            {
                continue;
            }
            const PassRecord& pass = m_builder->GetPass(passIndex);
            for (const ResourceAccess& access : pass.accesses)
            {
                const ResourceRecord& record = m_builder->GetResource(access.index);
                Format format = Format::Unknown;
                if (record.kind == ResourceKind::Texture)
                {
                    format = std::get<TextureDesc>(record.desc).format;
                }
                const Access required = InferAccess(access.mode, access.kind, format);
                if (!IsSingleKnownAccess(required))
                {
                    AddError(
                        ErrorCategory::UnknownAccess,
                        std::format("Plan() cannot map access for '{}'", record.name),
                        passIndex,
                        pass.name,
                        record.name);
                    continue;
                }
                plan.passes.push_back(PassResourceState{
                    passIndex,
                    pass.name,
                    access.index,
                    record.name,
                    access.kind,
                    access.mode,
                    current[access.index],
                    required,
                    required});
                current[access.index] = required;
            }
        }

        if (m_errors.size() != errorCountBefore)
        {
            return;
        }

        for (uint32_t index = 0; index < resourceCount; ++index)
        {
            if (current[index] != plan.resources[index].final)
            {
                plan.restores.push_back(ResourceRestore{
                    index,
                    plan.resources[index].name,
                    current[index],
                    plan.resources[index].final});
            }
        }

        m_accessPlan = std::move(plan);
        m_planned = true;
    }

    void GraphExecutor::Allocate()
    {
        if (!m_compileSuccess)
        {
            AddError(
                ErrorCategory::InvalidPass,
                "Allocate() requires a successful compile",
                Error::kNoPass,
                "",
                "");
            return;
        }
        if (m_allocated)
        {
            AddError(
                ErrorCategory::IncompatibleAccess,
                "Allocate() already ran",
                Error::kNoPass,
                "",
                "");
            return;
        }

        m_allocated = true;
        const size_t resourceCount = m_builder->GetResourceCount();
        const TransientReusePlan reuse = PlanTransientReuse(
            *m_builder, m_resourceLifetimes, m_livePassOrder, m_transientReuse);
        if (m_device != nullptr)
        {
            m_gpu = std::make_unique<GpuStorage>();
            m_gpu->textures.resize(resourceCount);
            m_gpu->buffers.resize(resourceCount);
        }

        const auto estimatedBytesOf = [](const ResourceRecord& record) -> uint64_t
        {
            if (record.kind == ResourceKind::Texture)
            {
                const TextureDesc& desc = std::get<TextureDesc>(record.desc);
                return static_cast<uint64_t>(desc.width) * desc.height * BytesPerPixel(desc.format);
            }
            const BufferDesc& desc = std::get<BufferDesc>(record.desc);
            return static_cast<uint64_t>(desc.bytesPerElement) * desc.numElements;
        };
        const auto account = [&](const ResourceRecord& record, bool physical)
        {
            const uint64_t bytes = estimatedBytesOf(record);
            if (record.kind == ResourceKind::Texture)
            {
                ++m_stats.logicalTextureCount;
                if (physical)
                {
                    ++m_stats.textureCount;
                }
            }
            else
            {
                ++m_stats.logicalBufferCount;
                if (physical)
                {
                    ++m_stats.bufferCount;
                }
            }
            m_stats.peakLogicalBytes += bytes;
            if (physical)
            {
                m_stats.estimatedBytes += bytes;
                m_stats.peakPhysicalBytes += bytes;
            }
        };

        std::vector<uint8_t> created(resourceCount, 0);
        for (uint32_t index = 0; index < resourceCount; ++index)
        {
            const ResourceRecord& record = m_builder->GetResource(index);
            if (record.imported)
            {
                continue;
            }

            if (record.kind == ResourceKind::Texture)
            {
                const TextureDesc& desc = std::get<TextureDesc>(record.desc);
                if (MakeTextureDesc(desc).format == nvrhi::Format::UNKNOWN)
                {
                    AddError(
                        ErrorCategory::InvalidDescriptor,
                        std::format("Allocate() cannot map format for texture '{}'", record.name),
                        Error::kNoPass,
                        "",
                        record.name);
                    continue;
                }
            }

            if (index >= reuse.physicalOwner.size() || reuse.physicalOwner[index] != index)
            {
                continue;
            }

            if (record.kind == ResourceKind::Texture)
            {
                const TextureDesc& desc = std::get<TextureDesc>(record.desc);
                const nvrhi::TextureDesc nv = MakeTextureDesc(desc);
                if (m_device != nullptr)
                {
                    nvrhi::TextureHandle handle = m_device->createTexture(nv);
                    if (!handle)
                    {
                        AddError(
                            ErrorCategory::AllocationFailed,
                            std::format("Allocate() failed to create texture '{}'", record.name),
                            Error::kNoPass,
                            "",
                            record.name);
                        continue;
                    }
                    nvrhi::ITexture* const native = handle;
                    m_gpu->textures[index] = std::move(handle);
                    m_registry.SetTexture(index, PhysicalTexture{native, record.name});
                }
                else
                {
                    auto stub = std::make_unique<CpuIdentity>();
                    stub->index = index;
                    m_registry.SetTexture(index, PhysicalTexture{stub.get(), record.name});
                    m_cpuTextures.push_back(std::move(stub));
                }
            }
            else
            {
                const BufferDesc& desc = std::get<BufferDesc>(record.desc);
                const nvrhi::BufferDesc nv = MakeBufferDesc(desc);
                if (m_device != nullptr)
                {
                    nvrhi::BufferHandle handle = m_device->createBuffer(nv);
                    if (!handle)
                    {
                        AddError(
                            ErrorCategory::AllocationFailed,
                            std::format("Allocate() failed to create buffer '{}'", record.name),
                            Error::kNoPass,
                            "",
                            record.name);
                        continue;
                    }
                    nvrhi::IBuffer* const native = handle;
                    m_gpu->buffers[index] = std::move(handle);
                    m_registry.SetBuffer(index, PhysicalBuffer{native, record.name});
                }
                else
                {
                    auto stub = std::make_unique<CpuIdentity>();
                    stub->index = index;
                    m_registry.SetBuffer(index, PhysicalBuffer{stub.get(), record.name});
                    m_cpuBuffers.push_back(std::move(stub));
                }
            }

            created[index] = 1;
            account(record, true);
        }

        for (const ReusePair& pair : reuse.reusePairs)
        {
            const bool ownerReady =
                pair.ownerIndex < created.size() && pair.aliasIndex < resourceCount && created[pair.ownerIndex] != 0;
            if (!ownerReady)
            {
                const std::string aliasName = pair.aliasIndex < resourceCount
                    ? m_builder->GetResource(pair.aliasIndex).name
                    : pair.aliasName;
                AddError(
                    ErrorCategory::AllocationFailed,
                    std::format(
                        "Allocate() failed to alias '{}' onto '{}'",
                        aliasName,
                        pair.ownerName),
                    Error::kNoPass,
                    "",
                    aliasName);
                continue;
            }

            const ResourceRecord& record = m_builder->GetResource(pair.aliasIndex);
            void* native = nullptr;
            if (record.kind == ResourceKind::Texture)
            {
                const PhysicalTexture* owner = m_registry.GetTexture(pair.ownerIndex);
                native = owner != nullptr ? owner->native : nullptr;
            }
            else
            {
                const PhysicalBuffer* owner = m_registry.GetBuffer(pair.ownerIndex);
                native = owner != nullptr ? owner->native : nullptr;
            }
            if (native == nullptr)
            {
                AddError(
                    ErrorCategory::AllocationFailed,
                    std::format(
                        "Allocate() failed to alias '{}' onto '{}'",
                        record.name,
                        pair.ownerName),
                    Error::kNoPass,
                    "",
                    record.name);
                continue;
            }
            if (record.kind == ResourceKind::Texture)
            {
                m_registry.SetTexture(pair.aliasIndex, PhysicalTexture{native, record.name});
            }
            else
            {
                m_registry.SetBuffer(pair.aliasIndex, PhysicalBuffer{native, record.name});
            }
            account(record, false);
            m_stats.reusePairs.push_back(pair);
        }

        m_stats.savedBytes = m_stats.peakLogicalBytes - m_stats.peakPhysicalBytes;
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

    nvrhi::ICommandList* GraphExecutor::SelectCommandList(
        uint32_t passIndex,
        nvrhi::ICommandList* graphicsCommandList) const
    {
        // Raster passes use the graphics list. A later PassFlags value can
        // choose a different list here without changing Execute()'s loop.
        (void)passIndex;
        return graphicsCommandList;
    }

    bool GraphExecutor::RunLivePass(nvrhi::ICommandList* commandList, uint32_t passIndex)
    {
        const std::string& passName = m_builder->GetPass(passIndex).name;
        const PassLambda* lambda = m_builder->FindLambda(passIndex);
        if (lambda == nullptr)
        {
            AddError(
                ErrorCategory::InvalidPass,
                std::format("Execute() pass '{}' has no lambda", passName),
                passIndex,
                passName,
                "");
            return false;
        }

        m_context.Activate(*this, passIndex, m_builder->GetPass(passIndex).accesses);
        (*lambda)(commandList, m_context);
        m_context.Deactivate();
        return true;
    }

    void GraphExecutor::Execute(nvrhi::ICommandList* graphicsCommandList)
    {
        if (!m_compileSuccess)
        {
            AddError(
                ErrorCategory::InvalidPass,
                "Execute() on a failed compile",
                Error::kNoPass,
                "",
                "");
            return;
        }

        for (const uint32_t passIndex : m_livePassOrder)
        {
            if (passIndex >= m_cullStates.size() || m_cullStates[passIndex].culled)
            {
                continue;
            }
            nvrhi::ICommandList* commandList = SelectCommandList(passIndex, graphicsCommandList);
            if (!RunLivePass(commandList, passIndex))
            {
                return;
            }
        }
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
