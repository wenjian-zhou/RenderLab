#pragma once

#include "GraphBuilder.h"

namespace renderlab::rdg
{
    struct EdgeResource
    {
        ResourceKind kind = ResourceKind::Texture;
        uint32_t resourceIndex = 0;

        friend bool operator==(const EdgeResource&, const EdgeResource&) = default;
    };

    struct DependencyEdge
    {
        uint32_t fromPass = 0;
        uint32_t toPass = 0;
        std::vector<EdgeResource> resources;

        friend bool operator==(const DependencyEdge&, const DependencyEdge&) = default;
    };

    enum class CullReason : uint8_t
    {
        CullingDisabled,
        RootOutput,
        RootNeverCull,
        Producer,
        UnusedLeaf,
        UnusedChain,
    };

    struct PassCullState
    {
        uint32_t passIndex = 0;
        bool culled = true;
        CullReason reason = CullReason::UnusedLeaf;

        friend bool operator==(const PassCullState&, const PassCullState&) = default;
    };

    struct ResourceLifetime
    {
        uint32_t resourceIndex = 0;
        uint32_t firstPass = 0;
        uint32_t lastPass = 0;
        bool imported = false;
        bool exported = false;

        friend bool operator==(const ResourceLifetime&, const ResourceLifetime&) = default;
    };

    struct CompileOptions
    {
        bool disableCulling = false;
    };

    class CompileResult
    {
    public:
        bool IsSuccess() const { return m_errors.empty(); }
        std::span<const uint32_t> GetPassOrder() const { return m_passOrder; }
        std::span<const uint32_t> GetLivePassOrder() const { return m_livePassOrder; }
        std::span<const PassCullState> GetPassCullStates() const { return m_cullStates; }
        std::span<const DependencyEdge> GetEdges() const { return m_edges; }
        std::span<const DependencyEdge> GetDependencies() const { return m_edges; }
        std::span<const Error> GetErrors() const { return m_errors; }
        std::span<const ResourceLifetime> GetResourceLifetimes() const { return m_resourceLifetimes; }
        std::string Dump() const;
        std::string DumpDot() const;

    private:
        friend class GraphCompiler;

        std::vector<uint32_t> m_passOrder;
        std::vector<uint32_t> m_livePassOrder;
        std::vector<PassCullState> m_cullStates;
        std::vector<DependencyEdge> m_edges;
        std::vector<Error> m_errors;
        std::vector<std::string> m_passNames;
        std::vector<std::string> m_resourceNames;
        std::vector<PassFlags> m_passFlags;
        std::vector<std::vector<ResourceAccess>> m_passAccesses;
        std::vector<ResourceKind> m_resourceKinds;
        std::vector<uint8_t> m_resourceImported;
        std::vector<uint8_t> m_resourceExported;
        std::vector<ResourceLifetime> m_resourceLifetimes;
        bool m_cullingApplied = false;
        bool m_lifetimesApplied = false;
    };

    class GraphCompiler
    {
    public:
        static CompileResult Compile(const GraphBuilder& builder, CompileOptions options = {});

    private:
        static void ApplyCulling(CompileResult& result, const GraphBuilder& builder, const CompileOptions& options);
        static void AnalyzeLifetimes(CompileResult& result, const GraphBuilder& builder);
    };
}
