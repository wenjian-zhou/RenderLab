#pragma once

#include "Handle.h"
#include "Pass.h"
#include "ResourceDesc.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace renderlab::rdg
{
    enum class ErrorCategory : uint8_t
    {
        NullHandle,
        ForeignGraph,
        StaleVersion,
        TypeMismatch,
        InvalidName,
        InvalidDescriptor,
        InvalidPassFlags,
    };

    // One rejected declaration or creation call. S4.6 turns these into the
    // compile error categories; naming both the pass and the resource is
    // part of the contract from day one.
    struct Error
    {
        static constexpr uint32_t kNoPass = 0xFFFFFFFFu;

        ErrorCategory category = ErrorCategory::NullHandle;
        std::string message;
        uint32_t passIndex = kNoPass;
        std::string passName;
        std::string resourceName;
    };

    // Builder-side registry entry. Indices are never recycled within a
    // graph, so version is the only staleness axis (S4.2 bumps it on writes).
    struct ResourceRecord
    {
        std::string name;
        ResourceKind kind = ResourceKind::Texture;
        std::variant<TextureDesc, BufferDesc> desc;
        bool imported = false; // UE bExternal
        bool exported = false; // UE bExtracted; imported || exported is a cull root (S4.4)
        uint32_t currentVersion = 0;
    };

    class GraphBuilder;

    // Declaration scope for one pass, returned by GraphBuilder::addPass. A
    // default PassBuilder is invalid; read/write on it are no-ops.
    class PassBuilder
    {
    public:
        bool isValid() const { return m_builder != nullptr; }
        uint32_t passIndex() const { return m_passIndex; }

        void read(TextureHandle handle);
        void read(BufferHandle handle);
        void write(TextureHandle handle);
        void write(BufferHandle handle);

    private:
        friend class GraphBuilder;

        PassBuilder() = default;
        PassBuilder(GraphBuilder& builder, uint32_t passIndex);

        GraphBuilder* m_builder = nullptr;
        uint32_t m_passIndex = 0;
    };

    // S4.1 declaration surface of the mini RDG: logical resources, pass
    // records, explicit read/write declarations. No execution, no
    // compilation, no NVRHI. Errors are collected, never thrown: a rejected
    // call records Error(s) and leaves the graph unchanged by that call;
    // assertNoErrors() is the explicit Debug trap for non-test callers.
    class GraphBuilder
    {
    public:
        GraphBuilder();

        GraphBuilder(const GraphBuilder&) = delete;
        GraphBuilder& operator=(const GraphBuilder&) = delete;

        uint32_t graphId() const { return m_graphId; }

        TextureHandle createTexture(const TextureDesc& desc);
        BufferHandle createBuffer(const BufferDesc& desc);
        TextureHandle importTexture(const TextureDesc& desc);
        BufferHandle importBuffer(const BufferDesc& desc);

        PassBuilder addPass(std::string_view name, PassFlags flags);

        void exportTexture(TextureHandle handle);
        void exportBuffer(BufferHandle handle);

        std::span<const Error> errors() const { return m_errors; }
        void assertNoErrors() const;

        size_t passCount() const { return m_passes.size(); }
        const PassRecord& pass(uint32_t passIndex) const;
        size_t resourceCount() const { return m_resources.size(); }
        const ResourceRecord& resource(uint32_t resourceIndex) const;

    private:
        friend class PassBuilder;

        TextureHandle createTextureResource(const TextureDesc& desc, bool imported);
        BufferHandle createBufferResource(const BufferDesc& desc, bool imported);
        bool validateTextureDesc(const TextureDesc& desc);
        bool validateBufferDesc(const BufferDesc& desc);

        void declareAccess(uint32_t passIndex, TextureHandle handle, AccessMode mode);
        void declareAccess(uint32_t passIndex, BufferHandle handle, AccessMode mode);
        void declareAccessInternal(
            uint32_t passIndex,
            ResourceKind kind,
            uint32_t index,
            uint32_t version,
            uint32_t graphId,
            AccessMode mode);
        void exportResource(ResourceKind kind, uint32_t index, uint32_t version, uint32_t graphId);
        const ResourceRecord* resolveHandle(
            ResourceKind kind,
            uint32_t index,
            uint32_t version,
            uint32_t graphId,
            uint32_t passIndex,
            const std::string& passName,
            const char* operation);
        void addError(
            ErrorCategory category,
            std::string message,
            uint32_t passIndex,
            std::string passName,
            std::string resourceName);

        uint32_t m_graphId;
        std::vector<ResourceRecord> m_resources;
        std::vector<PassRecord> m_passes;
        std::vector<Error> m_errors;
    };
}
