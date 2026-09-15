#pragma once

#include "../GraphCompiler.h"
#include "PassContext.h"
#include "PhysicalRegistry.h"
#include "PhysicalResource.h"

#include <functional>
#include <span>
#include <string>
#include <vector>

namespace renderlab::rdg::exec
{
    // Binds logical imported handles to opaque physical tokens and runs
    // synthetic pass callbacks. The builder must outlive the executor.
    // Compile result is read in the constructor (success flag and cull states
    // are copied); it does not need to outlive ExecutePass.
    class GraphExecutor
    {
    public:
        GraphExecutor(const GraphBuilder& builder, const CompileResult& result);

        GraphExecutor(const GraphExecutor&) = delete;
        GraphExecutor& operator=(const GraphExecutor&) = delete;

        void RegisterImport(TextureHandle handle, PhysicalTexture physical);
        void RegisterImport(BufferHandle handle, PhysicalBuffer physical);

        const PhysicalTexture* GetExported(TextureHandle handle);
        const PhysicalBuffer* GetExported(BufferHandle handle);

        void ExecutePass(uint32_t passIndex, const std::function<void(PassContext&)>& callback);

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

        const GraphBuilder* m_builder = nullptr;
        bool m_compileSuccess = false;
        std::vector<PassCullState> m_cullStates;
        PhysicalRegistry m_registry;
        PassContext m_context;
        std::vector<Error> m_errors;
    };
}
