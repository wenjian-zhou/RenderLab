#pragma once

#include "Handle.h"
#include "Pass.h"
#include "PassContext.h"
#include "ResourceDesc.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace nvrhi
{
    class ICommandList;
}

namespace renderlab::rdg
{
    enum class ErrorCategory : uint8_t
    {
        NullHandle,
        ForeignGraph,
        TypeMismatch,
        InvalidName,
        InvalidDescriptor,
        InvalidPassFlags,
        ReadBeforeProduce,
        DuplicateWrite,
        IncompatibleAccess,
        ZeroUseAllocation,
        UndeclaredAccess,
        ExpiredContext,
        UnregisteredImport,
        InvalidPass,
        AllocationFailed,
        UnknownAccess,
    };

    // One collected failure: a rejected declaration/creation call, a
    // compile-time ZeroUseAllocation, or an execution-time lookup / allocate /
    // Plan failure. Dumps expose category plus pass and resource names.
    struct Error
    {
        static constexpr uint32_t kNoPass = 0xFFFFFFFFu;

        ErrorCategory category = ErrorCategory::NullHandle;
        std::string message;
        uint32_t passIndex = kNoPass;
        std::string passName;
        std::string resourceName;
    };

    struct ResourceRecord
    {
        std::string name;
        ResourceKind kind = ResourceKind::Texture;
        std::variant<TextureDesc, BufferDesc> desc;
        bool imported = false; // UE bExternal
        bool exported = false; // UE bExtracted; imported || exported is a cull root when written
        bool hasInitialAccess = false;
        Access initialAccess = Access::Unknown;
        bool hasFinalAccess = false;
        Access finalAccess = Access::Unknown;
    };

    class GraphBuilder;

    // Declaration scope for one pass, returned by GraphBuilder::AddPass. A
    // default PassBuilder is invalid; Use on it is a no-op.
    class PassBuilder
    {
    public:
        bool IsValid() const { return m_builder != nullptr; }
        uint32_t PassIndex() const { return m_passIndex; }

        void Use(TextureHandle handle, Access access);
        void Use(BufferHandle handle, Access access);

    private:
        friend class GraphBuilder;

        PassBuilder() = default;
        PassBuilder(GraphBuilder& builder, uint32_t passIndex);

        GraphBuilder* m_builder = nullptr;
        uint32_t m_passIndex = 0;
    };

    // Stored at AddPass time and invoked later by GraphExecutor::Execute.
    // The command list is the one Execute selected for this pass. Device-free
    // tests pass nullptr. PassRecord stays name, flags, and accesses.
    using PassLambda = std::function<void(nvrhi::ICommandList*, PassContext&)>;

    class GraphBuilder
    {
    public:
        GraphBuilder();

        GraphBuilder(const GraphBuilder&) = delete;
        GraphBuilder& operator=(const GraphBuilder&) = delete;

        uint32_t GetGraphId() const { return m_graphId; }

        TextureHandle CreateTexture(const TextureDesc& desc);
        BufferHandle CreateBuffer(const BufferDesc& desc);
        TextureHandle ImportTexture(const TextureDesc& desc);
        BufferHandle ImportBuffer(const BufferDesc& desc);

        PassBuilder AddPass(std::string_view name, PassFlags flags);
        PassBuilder AddPass(std::string_view name, PassFlags flags, PassLambda lambda);

        // Stores the lambda when passIndex was added. An empty function clears
        // the slot. An out-of-range index records InvalidPass and does not store.
        void SetLambda(uint32_t passIndex, PassLambda lambda);

        const PassLambda* FindLambda(uint32_t passIndex) const;

        void SetInitialAccess(TextureHandle handle, Access access);
        void SetInitialAccess(BufferHandle handle, Access access);
        void ExportTexture(TextureHandle handle, Access finalAccess);
        void ExportBuffer(BufferHandle handle, Access finalAccess);

        std::span<const Error> GetErrors() const { return m_errors; }
        void AssertNoErrors() const;

        size_t GetPassCount() const { return m_passes.size(); }
        const PassRecord& GetPass(uint32_t passIndex) const;
        size_t GetResourceCount() const { return m_resources.size(); }
        const ResourceRecord& GetResource(uint32_t resourceIndex) const;

    private:
        friend class PassBuilder;

        TextureHandle CreateTextureResource(const TextureDesc& desc, bool imported);
        BufferHandle CreateBufferResource(const BufferDesc& desc, bool imported);
        bool ValidateTextureDesc(const TextureDesc& desc);
        bool ValidateBufferDesc(const BufferDesc& desc);

        bool DeclareUse(uint32_t passIndex, TextureHandle handle, Access access);
        bool DeclareUse(uint32_t passIndex, BufferHandle handle, Access access);
        bool DeclareUseInternal(
            uint32_t passIndex,
            ResourceKind kind,
            uint32_t index,
            uint32_t graphId,
            Access access);
        void SetInitialAccessInternal(
            ResourceKind kind,
            uint32_t index,
            uint32_t graphId,
            Access access);
        void ExportResource(
            ResourceKind kind,
            uint32_t index,
            uint32_t graphId,
            Access finalAccess);
        bool HasWritableUse(uint32_t index) const;
        const ResourceRecord* ResolveHandle(
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

        uint32_t m_graphId;
        std::vector<ResourceRecord> m_resources;
        std::vector<PassRecord> m_passes;
        std::vector<PassLambda> m_lambdas;
        std::vector<Error> m_errors;
    };
}
