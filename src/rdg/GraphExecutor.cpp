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

        const auto initialAccessOf = [](const ResourceRecord& record) -> Access
        {
            if (record.imported)
            {
                return record.hasInitialAccess ? record.initialAccess : Access::Unknown;
            }
            if (record.kind == ResourceKind::Buffer)
            {
                return Access::ShaderResource;
            }
            const Format format = std::get<TextureDesc>(record.desc).format;
            return format == Format::D32Float ? Access::DepthWrite : Access::RenderTarget;
        };

        for (uint32_t index = 0; index < resourceCount; ++index)
        {
            const ResourceRecord& record = m_builder->GetResource(index);
            const Access initial = initialAccessOf(record);
            if (record.imported && !record.hasInitialAccess)
            {
                AddError(
                    ErrorCategory::UnknownAccess,
                    std::format("Plan() has no initial access for imported '{}'", record.name),
                    Error::kNoPass,
                    "",
                    record.name);
                continue;
            }
            if (!IsSingleKnownAccess(initial))
            {
                AddError(
                    ErrorCategory::UnknownAccess,
                    std::format("Plan() cannot map access for '{}'", record.name),
                    Error::kNoPass,
                    "",
                    record.name);
                continue;
            }
            const Access finalAccess = record.exported && record.hasFinalAccess ? record.finalAccess : initial;
            plan.resources.push_back(ResourceBoundary{
                index,
                record.name,
                initial,
                finalAccess,
                record.imported,
                record.exported});
            current[index] = initial;
        }

        if (m_errors.size() != errorCountBefore)
        {
            return;
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
                const Access required = access.access;
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
            handle.graphId,
            Error::kNoPass,
            "",
            "import bind");
        if (record == nullptr)
        {
            return;
        }
        if (!record->imported)
        {
            AddError(
                ErrorCategory::IncompatibleAccess,
                std::format(
                    "cannot bind physical texture '{}' (index {}): resource is not imported",
                    record->name,
                    handle.index),
                Error::kNoPass,
                "",
                record->name);
            return;
        }
        if (physical.native != nullptr)
        {
            for (uint32_t index = 0; index < m_builder->GetResourceCount(); ++index)
            {
                if (index == handle.index)
                {
                    continue;
                }
                const PhysicalTexture* existing = m_registry.GetTexture(index);
                if (existing != nullptr && existing->native == physical.native)
                {
                    AddError(
                        ErrorCategory::IncompatibleAccess,
                        std::format(
                            "cannot bind physical texture '{}' (index {}): native is already bound to index {}",
                            record->name,
                            handle.index,
                            index),
                        Error::kNoPass,
                        "",
                        record->name);
                    return;
                }
            }
        }
        if (m_registry.HasTexture(handle.index))
        {
            const PhysicalTexture* existing = m_registry.GetTexture(handle.index);
            if (existing != nullptr && existing->native == physical.native)
            {
                return;
            }
            AddError(
                ErrorCategory::IncompatibleAccess,
                std::format(
                    "cannot bind physical texture '{}' (index {}): already registered",
                    record->name,
                    handle.index),
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
            handle.graphId,
            Error::kNoPass,
            "",
            "import bind");
        if (record == nullptr)
        {
            return;
        }
        if (!record->imported)
        {
            AddError(
                ErrorCategory::IncompatibleAccess,
                std::format(
                    "cannot bind physical buffer '{}' (index {}): resource is not imported",
                    record->name,
                    handle.index),
                Error::kNoPass,
                "",
                record->name);
            return;
        }
        if (physical.native != nullptr)
        {
            for (uint32_t index = 0; index < m_builder->GetResourceCount(); ++index)
            {
                if (index == handle.index)
                {
                    continue;
                }
                const PhysicalBuffer* existing = m_registry.GetBuffer(index);
                if (existing != nullptr && existing->native == physical.native)
                {
                    AddError(
                        ErrorCategory::IncompatibleAccess,
                        std::format(
                            "cannot bind physical buffer '{}' (index {}): native is already bound to index {}",
                            record->name,
                            handle.index,
                            index),
                        Error::kNoPass,
                        "",
                        record->name);
                    return;
                }
            }
        }
        if (m_registry.HasBuffer(handle.index))
        {
            const PhysicalBuffer* existing = m_registry.GetBuffer(handle.index);
            if (existing != nullptr && existing->native == physical.native)
            {
                return;
            }
            AddError(
                ErrorCategory::IncompatibleAccess,
                std::format(
                    "cannot bind physical buffer '{}' (index {}): already registered",
                    record->name,
                    handle.index),
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
            handle.graphId,
            Error::kNoPass,
            "",
            "export lookup");
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
            handle.graphId,
            Error::kNoPass,
            "",
            "export lookup");
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

    const PhysicalTexture* GraphExecutor::FindTexture(TextureHandle handle) const
    {
        if (handle.IsNull() || handle.graphId != m_builder->GetGraphId())
        {
            return nullptr;
        }
        if (handle.index >= m_builder->GetResourceCount())
        {
            return nullptr;
        }
        if (m_builder->GetResource(handle.index).kind != ResourceKind::Texture)
        {
            return nullptr;
        }
        return m_registry.GetTexture(handle.index);
    }

    const PhysicalBuffer* GraphExecutor::FindBuffer(BufferHandle handle) const
    {
        if (handle.IsNull() || handle.graphId != m_builder->GetGraphId())
        {
            return nullptr;
        }
        if (handle.index >= m_builder->GetResourceCount())
        {
            return nullptr;
        }
        if (m_builder->GetResource(handle.index).kind != ResourceKind::Buffer)
        {
            return nullptr;
        }
        return m_registry.GetBuffer(handle.index);
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

    void* GraphExecutor::FindNative(uint32_t resourceIndex) const
    {
        const ResourceRecord& record = m_builder->GetResource(resourceIndex);
        if (record.kind == ResourceKind::Texture)
        {
            const PhysicalTexture* physical = m_registry.GetTexture(resourceIndex);
            return physical != nullptr ? physical->native : nullptr;
        }
        const PhysicalBuffer* physical = m_registry.GetBuffer(resourceIndex);
        return physical != nullptr ? physical->native : nullptr;
    }

    void GraphExecutor::BeginTrackingPlanned(nvrhi::ICommandList* commandList)
    {
        for (const ResourceBoundary& resource : m_accessPlan.resources)
        {
            void* native = FindNative(resource.resourceIndex);
            if (native == nullptr)
            {
                continue;
            }
            const nvrhi::ResourceStates state = ToNvStates(resource.initial);
            const ResourceRecord& record = m_builder->GetResource(resource.resourceIndex);
            if (record.kind == ResourceKind::Texture)
            {
                nvrhi::ITexture* texture = static_cast<nvrhi::ITexture*>(native);
                if (texture->getDesc().keepInitialState)
                {
                    continue;
                }
                commandList->beginTrackingTextureState(texture, nvrhi::AllSubresources, state);
            }
            else
            {
                nvrhi::IBuffer* buffer = static_cast<nvrhi::IBuffer*>(native);
                if (buffer->getDesc().keepInitialState)
                {
                    continue;
                }
                commandList->beginTrackingBufferState(buffer, state);
            }
        }
    }

    bool GraphExecutor::QueueTransition(
        nvrhi::ICommandList* commandList,
        uint32_t resourceIndex,
        uint32_t passIndex,
        Access after)
    {
        if (resourceIndex >= m_trackedAccess.size() || m_trackedAccess[resourceIndex] == after)
        {
            return false;
        }

        const ResourceRecord& record = m_builder->GetResource(resourceIndex);
        const Access before = m_trackedAccess[resourceIndex];
        m_issuedTransitions.push_back(IssuedTransition{record.name, passIndex, before, after});

        void* native = FindNative(resourceIndex);
        bool queued = false;
        if (native == nullptr)
        {
            const std::string passName =
                passIndex == Error::kNoPass ? std::string() : m_builder->GetPass(passIndex).name;
            AddError(
                ErrorCategory::UnregisteredImport,
                std::format("barrier for unregistered resource '{}'", record.name),
                passIndex,
                passName,
                record.name);
        }
        else if (commandList != nullptr)
        {
            const nvrhi::ResourceStates state = ToNvStates(after);
            if (record.kind == ResourceKind::Texture)
            {
                commandList->setTextureState(
                    static_cast<nvrhi::ITexture*>(native), nvrhi::AllSubresources, state);
            }
            else
            {
                commandList->setBufferState(static_cast<nvrhi::IBuffer*>(native), state);
            }
            queued = true;
        }

        m_trackedAccess[resourceIndex] = after;
        return queued;
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
        m_issuedTransitions.clear();
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

        if (m_planned)
        {
            m_trackedAccess.assign(m_builder->GetResourceCount(), Access::Unknown);
            for (const ResourceBoundary& resource : m_accessPlan.resources)
            {
                if (resource.resourceIndex < m_trackedAccess.size())
                {
                    m_trackedAccess[resource.resourceIndex] = resource.initial;
                }
            }
            if (graphicsCommandList != nullptr)
            {
                BeginTrackingPlanned(graphicsCommandList);
            }
        }

        for (const uint32_t passIndex : m_livePassOrder)
        {
            if (passIndex >= m_cullStates.size() || m_cullStates[passIndex].culled)
            {
                continue;
            }
            nvrhi::ICommandList* commandList = SelectCommandList(passIndex, graphicsCommandList);
            if (m_planned)
            {
                bool queued = false;
                for (const ResourceAccess& access : m_builder->GetPass(passIndex).accesses)
                {
                    if (QueueTransition(commandList, access.index, passIndex, access.access))
                    {
                        queued = true;
                    }
                }
                if (queued && commandList != nullptr)
                {
                    commandList->commitBarriers();
                }
            }
            if (!RunLivePass(commandList, passIndex))
            {
                return;
            }
        }

        if (m_planned)
        {
            bool queued = false;
            for (const ResourceBoundary& resource : m_accessPlan.resources)
            {
                if (!resource.exported)
                {
                    continue;
                }
                if (QueueTransition(
                        graphicsCommandList, resource.resourceIndex, Error::kNoPass, resource.final))
                {
                    queued = true;
                }
            }
            if (queued && graphicsCommandList != nullptr)
            {
                graphicsCommandList->commitBarriers();
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
        uint32_t graphId,
        uint32_t passIndex,
        const std::string& passName,
        const char* operation)
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
                    "{} handle (index {}) in {} belongs to graph {}, not this graph {}",
                    ToString(kind),
                    index,
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
                ErrorCategory::IncompatibleAccess,
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
