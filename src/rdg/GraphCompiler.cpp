#include "GraphCompiler.h"

#include <algorithm>
#include <cassert>
#include <format>
#include <functional>
#include <map>
#include <queue>
#include <tuple>
#include <utility>

namespace renderlab::rdg
{
    namespace
    {
        const char* ToString(DependencyType type)
        {
            switch (type)
            {
            case DependencyType::RAW: return "RAW";
            case DependencyType::WAR: return "WAR";
            case DependencyType::WAW: return "WAW";
            }
            return "Unknown";
        }

        const char* ToString(ErrorCategory category)
        {
            switch (category)
            {
            case ErrorCategory::NullHandle: return "NullHandle";
            case ErrorCategory::ForeignGraph: return "ForeignGraph";
            case ErrorCategory::StaleVersion: return "StaleVersion";
            case ErrorCategory::TypeMismatch: return "TypeMismatch";
            case ErrorCategory::InvalidName: return "InvalidName";
            case ErrorCategory::InvalidDescriptor: return "InvalidDescriptor";
            case ErrorCategory::InvalidPassFlags: return "InvalidPassFlags";
            case ErrorCategory::ReadBeforeProduce: return "ReadBeforeProduce";
            case ErrorCategory::DuplicateWrite: return "DuplicateWrite";
            case ErrorCategory::SupersededUse: return "SupersededUse";
            case ErrorCategory::IncompatibleAccess: return "IncompatibleAccess";
            }
            return "Unknown";
        }

        std::string Quote(std::string_view value)
        {
            std::string quoted = "\"";
            for (const unsigned char character : value)
            {
                switch (character)
                {
                case '\\': quoted += "\\\\"; break;
                case '"': quoted += "\\\""; break;
                case '\n': quoted += "\\n"; break;
                case '\r': quoted += "\\r"; break;
                case '\t': quoted += "\\t"; break;
                default:
                    if (character < 0x20 || character == 0x7f)
                    {
                        quoted += std::format("\\x{:02X}", character);
                    }
                    else
                    {
                        quoted += static_cast<char>(character);
                    }
                    break;
                }
            }
            return quoted + '"';
        }

        std::string DumpReason(const DependencyReason& reason, const std::string& resourceName)
        {
            return std::format("{} resource {} {} v{} -> v{}", ToString(reason.type),
                reason.resourceIndex, Quote(resourceName), reason.sourceVersion, reason.targetVersion);
        }

        CycleDiagnostic FindCycle(
            const std::vector<DependencyEdge>& edges,
            const std::vector<std::vector<size_t>>& adjacency,
            const std::vector<size_t>& indegrees,
            const std::vector<std::string>& passNames,
            const std::vector<std::string>& resourceNames)
        {
            struct Frame
            {
                uint32_t passIndex;
                size_t nextEdge = 0;
            };

            std::vector<uint8_t> colors(adjacency.size(), 0);
            std::vector<size_t> parentEdges(adjacency.size(), edges.size());
            std::vector<Frame> stack;
            for (uint32_t start = 0; start < adjacency.size(); ++start)
            {
                if (indegrees[start] == 0 || colors[start] != 0)
                {
                    continue;
                }
                colors[start] = 1;
                stack.push_back({start});
                while (!stack.empty())
                {
                    Frame& frame = stack.back();
                    if (frame.nextEdge == adjacency[frame.passIndex].size())
                    {
                        colors[frame.passIndex] = 2;
                        stack.pop_back();
                        continue;
                    }
                    const size_t edgeIndex = adjacency[frame.passIndex][frame.nextEdge++];
                    const DependencyEdge& edge = edges[edgeIndex];
                    if (indegrees[edge.toPass] == 0 || colors[edge.toPass] == 2)
                    {
                        continue;
                    }
                    if (colors[edge.toPass] == 0)
                    {
                        colors[edge.toPass] = 1;
                        parentEdges[edge.toPass] = edgeIndex;
                        stack.push_back({edge.toPass});
                        continue;
                    }

                    std::vector<size_t> cycleEdges;
                    for (uint32_t current = edge.fromPass; current != edge.toPass;
                         current = edges[parentEdges[current]].fromPass)
                    {
                        cycleEdges.push_back(parentEdges[current]);
                    }
                    std::reverse(cycleEdges.begin(), cycleEdges.end());
                    cycleEdges.push_back(edgeIndex);
                    CycleDiagnostic cycle;
                    cycle.passes.push_back({edge.toPass, passNames[edge.toPass]});
                    for (const size_t cycleEdgeIndex : cycleEdges)
                    {
                        const DependencyEdge& cycleEdge = edges[cycleEdgeIndex];
                        const DependencyReason& reason = cycleEdge.reasons.front();
                        cycle.reasons.push_back({reason, resourceNames[reason.resourceIndex]});
                        cycle.passes.push_back({cycleEdge.toPass, passNames[cycleEdge.toPass]});
                    }
                    return cycle;
                }
            }
            assert(false && "Incomplete topological order must contain a cycle");
            return {};
        }
    }

    CompileResult GraphCompiler::Compile(const GraphBuilder& builder)
    {
        CompileResult result;
        for (uint32_t passIndex = 0; passIndex < builder.GetPassCount(); ++passIndex)
        {
            result.m_passNames.push_back(builder.GetPass(passIndex).name);
        }
        for (uint32_t resourceIndex = 0; resourceIndex < builder.GetResourceCount(); ++resourceIndex)
        {
            result.m_resourceNames.push_back(builder.GetResource(resourceIndex).name);
        }
        result.m_errors.assign(builder.GetErrors().begin(), builder.GetErrors().end());
        if (!result.m_errors.empty())
        {
            return result;
        }

        std::map<std::pair<uint32_t, uint32_t>, std::vector<DependencyReason>> mergedEdges;
        const auto addReason = [&](uint32_t fromPass, uint32_t toPass, DependencyReason reason)
        {
            if (fromPass != Error::kNoPass && toPass != Error::kNoPass)
            {
                mergedEdges[{fromPass, toPass}].push_back(reason);
            }
        };
        for (uint32_t resourceIndex = 0; resourceIndex < builder.GetResourceCount(); ++resourceIndex)
        {
            const ResourceRecord& resource = builder.GetResource(resourceIndex);
            for (uint32_t version = 0; version < resource.versions.size(); ++version)
            {
                const ResourceVersionRecord& current = resource.versions[version];
                for (const uint32_t reader : current.readerPasses)
                {
                    addReason(current.producerPass, reader, {DependencyType::RAW, resourceIndex, version, version});
                }
                if (version != 0)
                {
                    const ResourceVersionRecord& previous = resource.versions[version - 1];
                    for (const uint32_t reader : previous.readerPasses)
                    {
                        addReason(reader, current.producerPass, {DependencyType::WAR, resourceIndex, version - 1, version});
                    }
                    addReason(previous.producerPass, current.producerPass,
                        {DependencyType::WAW, resourceIndex, version - 1, version});
                }
            }
        }

        std::vector<std::vector<size_t>> adjacency(builder.GetPassCount());
        std::vector<size_t> indegrees(builder.GetPassCount(), 0);
        for (auto& [passes, reasons] : mergedEdges)
        {
            std::sort(reasons.begin(), reasons.end(), [](const DependencyReason& left, const DependencyReason& right)
            {
                return std::tie(left.resourceIndex, left.sourceVersion, left.targetVersion, left.type) <
                    std::tie(right.resourceIndex, right.sourceVersion, right.targetVersion, right.type);
            });
            reasons.erase(std::unique(reasons.begin(), reasons.end()), reasons.end());
            adjacency[passes.first].push_back(result.m_edges.size());
            ++indegrees[passes.second];
            result.m_edges.push_back({passes.first, passes.second, std::move(reasons)});
        }

        std::priority_queue<uint32_t, std::vector<uint32_t>, std::greater<uint32_t>> ready;
        for (uint32_t passIndex = 0; passIndex < builder.GetPassCount(); ++passIndex)
        {
            if (indegrees[passIndex] == 0)
            {
                ready.push(passIndex);
            }
        }
        while (!ready.empty())
        {
            const uint32_t passIndex = ready.top();
            ready.pop();
            result.m_passOrder.push_back(passIndex);
            for (const size_t edgeIndex : adjacency[passIndex])
            {
                const uint32_t nextPass = result.m_edges[edgeIndex].toPass;
                if (--indegrees[nextPass] == 0)
                {
                    ready.push(nextPass);
                }
            }
        }
        if (result.m_passOrder.size() != builder.GetPassCount())
        {
            result.m_passOrder.clear();
            result.m_cycle = FindCycle(result.m_edges, adjacency, indegrees, result.m_passNames, result.m_resourceNames);
        }
        return result;
    }

    std::string CompileResult::Dump() const
    {
        std::string dump = std::format("compile: {}\norder: [", IsSuccess() ? "success" : "failed");
        for (size_t orderIndex = 0; orderIndex < m_passOrder.size(); ++orderIndex)
        {
            const uint32_t passIndex = m_passOrder[orderIndex];
            dump += std::format("{}{} {}", orderIndex == 0 ? "" : ", ", passIndex, Quote(m_passNames[passIndex]));
        }
        dump += "]\nedges:\n";
        for (const DependencyEdge& edge : m_edges)
        {
            dump += std::format("  {} {} -> {} {}\n", edge.fromPass, Quote(m_passNames[edge.fromPass]),
                edge.toPass, Quote(m_passNames[edge.toPass]));
            for (const DependencyReason& reason : edge.reasons)
            {
                dump += std::format("    {}\n", DumpReason(reason, m_resourceNames[reason.resourceIndex]));
            }
        }
        dump += "errors:\n";
        for (const Error& error : m_errors)
        {
            dump += std::format("  {} pass={} {} resource={} {}\n", ToString(error.category),
                error.passIndex == Error::kNoPass ? "none" : std::to_string(error.passIndex),
                Quote(error.passName), Quote(error.resourceName), Quote(error.message));
        }
        dump += "cycle:";
        if (!m_cycle)
        {
            return dump + " none\n";
        }
        dump += "\n";
        for (size_t step = 0; step < m_cycle->reasons.size(); ++step)
        {
            const CyclePass& from = m_cycle->passes[step];
            const CyclePass& to = m_cycle->passes[step + 1];
            const CycleReason& reason = m_cycle->reasons[step];
            dump += std::format("  {} {} -> {} {}: {}\n", from.passIndex, Quote(from.name), to.passIndex,
                Quote(to.name), DumpReason(reason.dependency, reason.resourceName));
        }
        return dump;
    }
}
