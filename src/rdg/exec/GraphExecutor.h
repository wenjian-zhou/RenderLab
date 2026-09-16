#pragma once

#include "../GraphCompiler.h"
#include "AccessPlan.h"
#include "PassContext.h"
#include "PhysicalRegistry.h"
#include "PhysicalResource.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace nvrhi
{
    class IDevice;
}

namespace renderlab::rdg::exec
{
    struct AllocationStats
    {
        uint32_t textureCount = 0;
        uint32_t bufferCount = 0;
        uint64_t estimatedBytes = 0;
    };

    // Binds logical imported handles to opaque physical tokens, allocates
    // internal Create* identities, and runs synthetic pass callbacks. The
    // builder must outlive the executor. Compile result is read in the
    // constructor (success flag and cull states are copied); it does not
    // need to outlive ExecutePass.
    class GraphExecutor
    {
    public:
        GraphExecutor(const GraphBuilder& builder, const CompileResult& result);
        ~GraphExecutor();

        GraphExecutor(const GraphExecutor&) = delete;
        GraphExecutor& operator=(const GraphExecutor&) = delete;

        void SetDevice(nvrhi::IDevice* device);
        void Allocate();
        void Plan();
        AllocationStats GetAllocationStats() const { return m_stats; }

        void RegisterImport(TextureHandle handle, PhysicalTexture physical);
        void RegisterImport(BufferHandle handle, PhysicalBuffer physical);

        const PhysicalTexture* GetExported(TextureHandle handle);
        const PhysicalBuffer* GetExported(BufferHandle handle);

        void ExecutePass(uint32_t passIndex, const std::function<void(PassContext&)>& callback);

        const AccessPlan& GetAccessPlan() const { return m_accessPlan; }

        std::span<const Error> GetErrors() const { return m_errors; }
        void AssertNoErrors() const;

    private:
        friend class PassContext;

        enum class VersionRule : uint8_t
        {
            AnyInRange,
            Current,
        };

        const ResourceRecord* ValidateHandle(
            ResourceKind kind,
            uint32_t index,
            uint32_t version,
            uint32_t graphId,
            uint32_t passIndex,
            const std::string& passName,
            const char* operation,
            VersionRule versionRule);
        void AddError(
            ErrorCategory category,
            std::string message,
            uint32_t passIndex,
            std::string passName,
            std::string resourceName);

        struct CpuIdentity
        {
            uint32_t index = 0;
        };
        struct GpuStorage;

        const GraphBuilder* m_builder = nullptr;
        bool m_compileSuccess = false;
        std::vector<PassCullState> m_cullStates;
        std::vector<uint32_t> m_livePassOrder;
        PhysicalRegistry m_registry;
        PassContext m_context;
        std::vector<Error> m_errors;
        nvrhi::IDevice* m_device = nullptr;
        bool m_allocated = false;
        bool m_planned = false;
        AccessPlan m_accessPlan;
        AllocationStats m_stats{};
        std::vector<std::unique_ptr<CpuIdentity>> m_cpuTextures;
        std::vector<std::unique_ptr<CpuIdentity>> m_cpuBuffers;
        std::unique_ptr<GpuStorage> m_gpu;
    };
}
