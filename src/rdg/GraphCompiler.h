#pragma once

#include "GraphBuilder.h"

#include <optional>

namespace renderlab::rdg
{
    enum class DependencyType : uint8_t
    {
        RAW,
        WAR,
        WAW,
    };

    struct DependencyReason
    {
        DependencyType type = DependencyType::RAW;
        uint32_t resourceIndex = 0;
        uint32_t sourceVersion = 0;
        uint32_t targetVersion = 0;

        friend bool operator==(const DependencyReason&, const DependencyReason&) = default;
    };

    struct DependencyEdge
    {
        uint32_t fromPass = 0;
        uint32_t toPass = 0;
        std::vector<DependencyReason> reasons;

        friend bool operator==(const DependencyEdge&, const DependencyEdge&) = default;
    };

    struct CyclePass
    {
        uint32_t passIndex = 0;
        std::string name;

        friend bool operator==(const CyclePass&, const CyclePass&) = default;
    };

    struct CycleReason
    {
        DependencyReason dependency;
        std::string resourceName;

        friend bool operator==(const CycleReason&, const CycleReason&) = default;
    };

    struct CycleDiagnostic
    {
        std::vector<CyclePass> passes;
        std::vector<CycleReason> reasons;

        friend bool operator==(const CycleDiagnostic&, const CycleDiagnostic&) = default;
    };

    class CompileResult
    {
    public:
        bool IsSuccess() const { return m_errors.empty() && !m_cycle.has_value(); }
        std::span<const uint32_t> GetPassOrder() const { return m_passOrder; }
        std::span<const DependencyEdge> GetEdges() const { return m_edges; }
        std::span<const DependencyEdge> GetDependencies() const { return m_edges; }
        std::span<const Error> GetErrors() const { return m_errors; }
        const std::optional<CycleDiagnostic>& GetCycle() const { return m_cycle; }
        bool HasCycle() const { return m_cycle.has_value(); }
        std::string Dump() const;

    private:
        friend class GraphCompiler;

        std::vector<uint32_t> m_passOrder;
        std::vector<DependencyEdge> m_edges;
        std::vector<Error> m_errors;
        std::optional<CycleDiagnostic> m_cycle;
        std::vector<std::string> m_passNames;
        std::vector<std::string> m_resourceNames;
    };

    class GraphCompiler
    {
    public:
        static CompileResult Compile(const GraphBuilder& builder);
    };
}
