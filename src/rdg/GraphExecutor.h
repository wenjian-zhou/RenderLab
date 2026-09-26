#pragma once

#include "GraphCompiler.h"
#include "AccessPlan.h"
#include "PassContext.h"
#include "PhysicalRegistry.h"
#include "PhysicalResource.h"
#include "TransientReuse.h"

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace nvrhi
{
    class ICommandList;
    class IDevice;
}

namespace renderlab::rdg
{
    struct AllocationStats
    {
        // Physical objects actually created. Aliased logical resources are
        // not counted here.
        uint32_t textureCount = 0;
        uint32_t bufferCount = 0;
        uint64_t estimatedBytes = 0;
        // Every successful Create*, including resources that alias an owner.
        uint32_t logicalTextureCount = 0;
        uint32_t logicalBufferCount = 0;
        uint64_t peakLogicalBytes = 0;
        // Objects are held until the executor is destroyed, so the frame
        // high-water mark is the physical byte total.
        uint64_t peakPhysicalBytes = 0;
        uint64_t savedBytes = 0;
        std::vector<ReusePair> reusePairs;
    };

    // Binds logical imported handles to opaque physical tokens, allocates
    // internal Create* identities, plans access states, and runs the pass
    // lambdas stored on a GraphBuilder. The builder must outlive the
    // executor. Compile result is read in the constructor (success flag,
    // cull states, live pass order, and resource lifetimes are copied); it
    // does not need to outlive Plan, Allocate, or Execute.
    class GraphExecutor
    {
    public:
        GraphExecutor(const GraphBuilder& builder, const CompileResult& result);
        ~GraphExecutor();

        GraphExecutor(const GraphExecutor&) = delete;
        GraphExecutor& operator=(const GraphExecutor&) = delete;

        void SetDevice(nvrhi::IDevice* device);
        // Default is on. Must be called before Allocate(). A call after
        // Allocate() records IncompatibleAccess and leaves the flag unchanged.
        void SetTransientReuse(bool enabled);
        void Allocate();
        void Plan();
        AllocationStats GetAllocationStats() const { return m_stats; }

        void RegisterImport(TextureHandle handle, PhysicalTexture physical);
        void RegisterImport(BufferHandle handle, PhysicalBuffer physical);

        const PhysicalTexture* GetExported(TextureHandle handle);
        const PhysicalBuffer* GetExported(BufferHandle handle);

        // Walks the compiled live order and invokes each pass lambda.
        // graphicsCommandList is the list used for raster passes. Selection
        // of the list for a pass lives in SelectCommandList so a later flag
        // can choose another queue without changing this loop.
        void Execute(nvrhi::ICommandList* graphicsCommandList);

        const AccessPlan& GetAccessPlan() const { return m_accessPlan; }

        struct IssuedTransition
        {
            std::string resourceName;
            uint32_t passIndex = Error::kNoPass;
            Access before = Access::Unknown;
            Access after = Access::Unknown;
        };

        // Valid until the next Execute(). Empty when Plan() did not succeed.
        std::span<const IssuedTransition> GetIssuedTransitions() const { return m_issuedTransitions; }

        std::span<const Error> GetErrors() const { return m_errors; }
        void AssertNoErrors() const;

    private:
        friend class PassContext;

        const ResourceRecord* ValidateHandle(
            ResourceKind kind,
            uint32_t index,
            uint32_t graphId,
            uint32_t passIndex,
            const std::string& passName,
            const char* operation);
        void AddError(
            ErrorCategory category,
            std::string message,
            uint32_t passIndex,
            std::string passName,
            std::string resourceName);
        nvrhi::ICommandList* SelectCommandList(
            uint32_t passIndex,
            nvrhi::ICommandList* graphicsCommandList) const;
        bool RunLivePass(nvrhi::ICommandList* commandList, uint32_t passIndex);
        void BeginTrackingPlanned(nvrhi::ICommandList* commandList);
        bool QueueTransition(
            nvrhi::ICommandList* commandList,
            uint32_t resourceIndex,
            uint32_t passIndex,
            Access after);
        void* FindNative(uint32_t resourceIndex) const;

        struct CpuIdentity
        {
            uint32_t index = 0;
        };
        struct GpuStorage;

        const GraphBuilder* m_builder = nullptr;
        bool m_compileSuccess = false;
        std::vector<PassCullState> m_cullStates;
        std::vector<uint32_t> m_livePassOrder;
        std::vector<ResourceLifetime> m_resourceLifetimes;
        PhysicalRegistry m_registry;
        PassContext m_context;
        std::vector<Error> m_errors;
        nvrhi::IDevice* m_device = nullptr;
        bool m_allocated = false;
        bool m_transientReuse = true;
        bool m_planned = false;
        AccessPlan m_accessPlan;
        std::vector<Access> m_trackedAccess;
        std::vector<IssuedTransition> m_issuedTransitions;
        AllocationStats m_stats{};
        std::vector<std::unique_ptr<CpuIdentity>> m_cpuTextures;
        std::vector<std::unique_ptr<CpuIdentity>> m_cpuBuffers;
        std::unique_ptr<GpuStorage> m_gpu;
    };
}
