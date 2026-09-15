#include "GraphCompiler.h"

#include <algorithm>
#include <cassert>
#include <format>
#include <functional>
#include <map>
#include <queue>
#include <string_view>
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
            case ErrorCategory::ZeroUseAllocation: return "ZeroUseAllocation";
            case ErrorCategory::UndeclaredAccess: return "UndeclaredAccess";
            case ErrorCategory::ExpiredContext: return "ExpiredContext";
            case ErrorCategory::UnregisteredImport: return "UnregisteredImport";
            case ErrorCategory::InvalidPass: return "InvalidPass";
            }
            return "Unknown";
        }

        const char* ToString(CullReason reason)
        {
            switch (reason)
            {
            case CullReason::CullingDisabled: return "culling-disabled";
            case CullReason::RootOutput: return "root-output";
            case CullReason::RootNeverCull: return "root-never-cull";
            case CullReason::Producer: return "producer";
            case CullReason::UnusedLeaf: return "unused-leaf";
            case CullReason::UnusedChain: return "unused-chain";
            }
            return "unknown";
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

        const char* ToString(ResourceKind kind)
        {
            return kind == ResourceKind::Texture ? "texture" : "buffer";
        }

        const char* ToString(AccessMode mode)
        {
            return mode == AccessMode::Read ? "read" : "write";
        }

        std::string FormatFlags(PassFlags flags)
        {
            std::string text;
            if ((flags & PassFlags::Raster) != PassFlags::None)
            {
                text = "Raster";
            }
            if ((flags & PassFlags::NeverCull) != PassFlags::None)
            {
                if (!text.empty())
                {
                    text += " ";
                }
                text += "NeverCull";
            }
            return text.empty() ? std::string("None") : text;
        }

        std::string FormatPassIndex(uint32_t passIndex)
        {
            return passIndex == Error::kNoPass ? std::string("none") : std::to_string(passIndex);
        }

        std::string DumpReason(const DependencyReason& reason, const std::string& resourceName)
        {
            return std::format("{} resource {} {} v{} -> v{}", ToString(reason.type),
                reason.resourceIndex, Quote(resourceName), reason.sourceVersion, reason.targetVersion);
        }

        std::string DotQuote(std::string_view value)
        {
            std::string quoted = "\"";
            for (const unsigned char character : value)
            {
                switch (character)
                {
                case '\\': quoted += "\\\\"; break;
                case '"': quoted += "\\\""; break;
                case '\n': quoted += "\\n"; break;
                default:
                    quoted += static_cast<char>(character);
                    break;
                }
            }
            return quoted + '"';
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

        bool WritesCullRoot(const GraphBuilder& builder, uint32_t passIndex)
        {
            for (const ResourceAccess& access : builder.GetPass(passIndex).accesses)
            {
                if (access.mode != AccessMode::Write)
                {
                    continue;
                }
                const ResourceRecord& resource = builder.GetResource(access.index);
                if (resource.imported || resource.exported)
                {
                    return true;
                }
            }
            return false;
        }

        bool IsProducerEdge(const DependencyEdge& edge)
        {
            for (const DependencyReason& reason : edge.reasons)
            {
                if (reason.type == DependencyType::RAW || reason.type == DependencyType::WAW)
                {
                    return true;
                }
            }
            return false;
        }
    }

    void GraphCompiler::ApplyCulling(CompileResult& result, const GraphBuilder& builder, const CompileOptions& options)
    {
        const uint32_t passCount = static_cast<uint32_t>(builder.GetPassCount());
        result.m_cullStates.resize(passCount);
        if (options.disableCulling)
        {
            for (uint32_t passIndex = 0; passIndex < passCount; ++passIndex)
            {
                result.m_cullStates[passIndex] = {passIndex, false, CullReason::CullingDisabled};
            }
            result.m_livePassOrder = result.m_passOrder;
            result.m_cullingApplied = true;
            return;
        }

        std::vector<std::vector<uint32_t>> producers(passCount);
        std::vector<uint8_t> hasOutgoingProducerEdge(passCount, 0);
        for (const DependencyEdge& edge : result.m_edges)
        {
            if (!IsProducerEdge(edge))
            {
                continue;
            }
            producers[edge.toPass].push_back(edge.fromPass);
            hasOutgoingProducerEdge[edge.fromPass] = 1;
        }
        for (std::vector<uint32_t>& producerList : producers)
        {
            std::sort(producerList.begin(), producerList.end());
            producerList.erase(std::unique(producerList.begin(), producerList.end()), producerList.end());
        }

        std::vector<uint8_t> culled(passCount, 1);
        std::vector<CullReason> reasons(passCount, CullReason::UnusedLeaf);
        std::vector<uint32_t> stack;
        for (uint32_t passIndex = 0; passIndex < passCount; ++passIndex)
        {
            const bool neverCull =
                (builder.GetPass(passIndex).flags & PassFlags::NeverCull) != PassFlags::None;
            if (neverCull || WritesCullRoot(builder, passIndex))
            {
                reasons[passIndex] = neverCull ? CullReason::RootNeverCull : CullReason::RootOutput;
                stack.push_back(passIndex);
            }
        }
        while (!stack.empty())
        {
            const uint32_t passIndex = stack.back();
            stack.pop_back();
            if (culled[passIndex] == 0)
            {
                continue;
            }
            culled[passIndex] = 0;
            if (reasons[passIndex] != CullReason::RootNeverCull &&
                reasons[passIndex] != CullReason::RootOutput)
            {
                reasons[passIndex] = CullReason::Producer;
            }
            for (const uint32_t producer : producers[passIndex])
            {
                if (culled[producer] != 0)
                {
                    stack.push_back(producer);
                }
            }
        }

        for (uint32_t passIndex = 0; passIndex < passCount; ++passIndex)
        {
            if (culled[passIndex] != 0)
            {
                reasons[passIndex] = hasOutgoingProducerEdge[passIndex] != 0 ?
                    CullReason::UnusedChain : CullReason::UnusedLeaf;
            }
            result.m_cullStates[passIndex] = {passIndex, culled[passIndex] != 0, reasons[passIndex]};
        }
        for (const uint32_t passIndex : result.m_passOrder)
        {
            if (culled[passIndex] == 0)
            {
                result.m_livePassOrder.push_back(passIndex);
            }
        }
        result.m_cullingApplied = true;
    }

    void GraphCompiler::AnalyzeLifetimes(CompileResult& result, const GraphBuilder& builder)
    {
        struct VersionUse
        {
            bool seen = false;
            uint32_t firstSlot = 0;
            uint32_t lastSlot = 0;
            uint32_t firstPass = Error::kNoPass;
            uint32_t lastPass = Error::kNoPass;
        };

        const uint32_t resourceCount = static_cast<uint32_t>(builder.GetResourceCount());
        std::vector<std::vector<VersionUse>> uses(resourceCount);
        for (uint32_t resourceIndex = 0; resourceIndex < resourceCount; ++resourceIndex)
        {
            uses[resourceIndex].resize(builder.GetResource(resourceIndex).versions.size());
        }

        for (uint32_t slot = 0; slot < result.m_livePassOrder.size(); ++slot)
        {
            const uint32_t passIndex = result.m_livePassOrder[slot];
            for (const ResourceAccess& access : builder.GetPass(passIndex).accesses)
            {
                VersionUse& use = uses[access.index][access.version];
                if (!use.seen)
                {
                    use.seen = true;
                    use.firstSlot = slot;
                    use.firstPass = passIndex;
                }
                use.lastSlot = slot;
                use.lastPass = passIndex;
            }
        }

        const uint32_t firstLivePass =
            result.m_livePassOrder.empty() ? Error::kNoPass : result.m_livePassOrder.front();
        const uint32_t lastLivePass =
            result.m_livePassOrder.empty() ? Error::kNoPass : result.m_livePassOrder.back();

        for (uint32_t resourceIndex = 0; resourceIndex < resourceCount; ++resourceIndex)
        {
            const ResourceRecord& resource = builder.GetResource(resourceIndex);
            if (resource.imported && !uses[resourceIndex].empty() && !uses[resourceIndex][0].seen)
            {
                VersionUse& v0 = uses[resourceIndex][0];
                v0.seen = true;
                v0.firstSlot = 0;
                v0.lastSlot = 0;
                v0.firstPass = firstLivePass;
                v0.lastPass = firstLivePass;
            }

            bool hasLiveAccess = false;
            for (const VersionUse& use : uses[resourceIndex])
            {
                if (use.seen)
                {
                    hasLiveAccess = true;
                    break;
                }
            }
            if (!hasLiveAccess && !resource.imported)
            {
                result.m_errors.push_back({
                    ErrorCategory::ZeroUseAllocation,
                    std::format("resource '{}' is a zero-use non-imported allocation", resource.name),
                    Error::kNoPass,
                    "",
                    resource.name,
                });
            }
        }
        if (!result.m_errors.empty())
        {
            return;
        }

        for (uint32_t resourceIndex = 0; resourceIndex < resourceCount; ++resourceIndex)
        {
            const ResourceRecord& resource = builder.GetResource(resourceIndex);
            bool any = false;
            uint32_t minSlot = 0;
            uint32_t maxSlot = 0;
            uint32_t minPass = Error::kNoPass;
            uint32_t maxPass = Error::kNoPass;
            for (uint32_t version = 0; version < uses[resourceIndex].size(); ++version)
            {
                const VersionUse& use = uses[resourceIndex][version];
                if (!use.seen)
                {
                    continue;
                }
                result.m_versionLifetimes.push_back({resourceIndex, version, use.firstPass, use.lastPass});
                if (!any || use.firstSlot < minSlot)
                {
                    minSlot = use.firstSlot;
                    minPass = use.firstPass;
                }
                if (!any || use.lastSlot > maxSlot)
                {
                    maxSlot = use.lastSlot;
                    maxPass = use.lastPass;
                }
                any = true;
            }
            if (!any)
            {
                continue;
            }
            if (resource.imported)
            {
                minPass = firstLivePass;
            }
            if (resource.exported)
            {
                maxPass = lastLivePass;
            }
            result.m_resourceLifetimes.push_back(
                {resourceIndex, minPass, maxPass, resource.imported, resource.exported});
        }
        result.m_lifetimesApplied = true;
    }

    CompileResult GraphCompiler::Compile(const GraphBuilder& builder, CompileOptions options)
    {
        CompileResult result;
        for (uint32_t passIndex = 0; passIndex < builder.GetPassCount(); ++passIndex)
        {
            const PassRecord& pass = builder.GetPass(passIndex);
            result.m_passNames.push_back(pass.name);
            result.m_passFlags.push_back(pass.flags);
            result.m_passAccesses.push_back(pass.accesses);
        }
        for (uint32_t resourceIndex = 0; resourceIndex < builder.GetResourceCount(); ++resourceIndex)
        {
            const ResourceRecord& resource = builder.GetResource(resourceIndex);
            result.m_resourceNames.push_back(resource.name);
            result.m_resourceKinds.push_back(resource.kind);
            result.m_resourceImported.push_back(resource.imported ? 1 : 0);
            result.m_resourceExported.push_back(resource.exported ? 1 : 0);
            result.m_resourceVersions.push_back(resource.versions);
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
            return result;
        }
        ApplyCulling(result, builder, options);
        AnalyzeLifetimes(result, builder);
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
            dump += " none\n";
        }
        else
        {
            dump += "\n";
            for (size_t step = 0; step < m_cycle->reasons.size(); ++step)
            {
                const CyclePass& from = m_cycle->passes[step];
                const CyclePass& to = m_cycle->passes[step + 1];
                const CycleReason& reason = m_cycle->reasons[step];
                dump += std::format("  {} {} -> {} {}: {}\n", from.passIndex, Quote(from.name), to.passIndex,
                    Quote(to.name), DumpReason(reason.dependency, reason.resourceName));
            }
        }
        if (!m_cullingApplied)
        {
            dump += "cull: skipped\n";
        }
        else
        {
            dump += "cull:\n";
            for (const PassCullState& state : m_cullStates)
            {
                dump += std::format("  {} {} {} {}\n", state.passIndex, Quote(m_passNames[state.passIndex]),
                    state.culled ? "culled" : "live", ToString(state.reason));
            }
        }
        if (!m_lifetimesApplied)
        {
            dump += "lifetime: skipped\n";
        }
        else
        {
            dump += "lifetime:\n";
            for (const ResourceLifetime& lifetime : m_resourceLifetimes)
            {
                dump += std::format("  resource {} {} first={} last={}", lifetime.resourceIndex,
                    Quote(m_resourceNames[lifetime.resourceIndex]), FormatPassIndex(lifetime.firstPass),
                    FormatPassIndex(lifetime.lastPass));
                if (lifetime.imported)
                {
                    dump += " imported";
                }
                if (lifetime.exported)
                {
                    dump += " exported";
                }
                dump += "\n";
            }
            for (const VersionLifetime& lifetime : m_versionLifetimes)
            {
                dump += std::format("  version {} {} v{} first={} last={}\n", lifetime.resourceIndex,
                    Quote(m_resourceNames[lifetime.resourceIndex]), lifetime.version,
                    FormatPassIndex(lifetime.firstPass), FormatPassIndex(lifetime.lastPass));
            }
        }
        dump += "flags:\n";
        for (uint32_t passIndex = 0; passIndex < m_passNames.size(); ++passIndex)
        {
            dump += std::format("  {} {} {}\n", passIndex, Quote(m_passNames[passIndex]),
                FormatFlags(m_passFlags[passIndex]));
        }
        dump += "accesses:\n";
        for (uint32_t passIndex = 0; passIndex < m_passNames.size(); ++passIndex)
        {
            dump += std::format("  {} {}\n", passIndex, Quote(m_passNames[passIndex]));
            for (const ResourceAccess& access : m_passAccesses[passIndex])
            {
                dump += std::format("    {} {} {} {} v{}\n", ToString(access.mode), ToString(access.kind),
                    access.index, Quote(m_resourceNames[access.index]), access.version);
            }
        }
        dump += "versions:\n";
        for (uint32_t resourceIndex = 0; resourceIndex < m_resourceNames.size(); ++resourceIndex)
        {
            dump += std::format("  {} {} {}", ToString(m_resourceKinds[resourceIndex]), resourceIndex,
                Quote(m_resourceNames[resourceIndex]));
            if (m_resourceImported[resourceIndex] != 0)
            {
                dump += " imported";
            }
            if (m_resourceExported[resourceIndex] != 0)
            {
                dump += " exported";
            }
            dump += "\n";
            for (uint32_t version = 0; version < m_resourceVersions[resourceIndex].size(); ++version)
            {
                const ResourceVersionRecord& record = m_resourceVersions[resourceIndex][version];
                dump += std::format("    v{} producer=", version);
                if (record.producerPass != Error::kNoPass)
                {
                    dump += std::format("{} {}", record.producerPass, Quote(m_passNames[record.producerPass]));
                }
                else
                {
                    dump += record.produced ? "external" : "unproduced";
                }
                dump += " readers=[";
                for (size_t reader = 0; reader < record.readerPasses.size(); ++reader)
                {
                    const uint32_t passIndex = record.readerPasses[reader];
                    dump += std::format("{}{} {}", reader == 0 ? "" : ", ", passIndex, Quote(m_passNames[passIndex]));
                }
                dump += "]\n";
            }
        }
        return dump;
    }

    std::string CompileResult::DumpDot() const
    {
        std::string dump = "digraph RDG {\n  rankdir=LR;\n";
        for (uint32_t passIndex = 0; passIndex < m_passNames.size(); ++passIndex)
        {
            std::string label = std::format("{} {}\n{}", passIndex, Quote(m_passNames[passIndex]),
                FormatFlags(m_passFlags[passIndex]));
            if (m_cullingApplied)
            {
                const PassCullState& state = m_cullStates[passIndex];
                label += std::format("\n{} {}", state.culled ? "culled" : "live", ToString(state.reason));
            }
            dump += std::format("  p{} [label={}];\n", passIndex, DotQuote(label));
        }
        for (const DependencyEdge& edge : m_edges)
        {
            for (const DependencyReason& reason : edge.reasons)
            {
                const std::string label = std::format("{} {} v{}", ToString(reason.type),
                    Quote(m_resourceNames[reason.resourceIndex]), reason.sourceVersion);
                dump += std::format("  p{} -> p{} [label={}];\n", edge.fromPass, edge.toPass, DotQuote(label));
            }
        }
        if (!m_resourceNames.empty())
        {
            dump += "  subgraph cluster_resources {\n    label=\"resources\";\n";
            for (uint32_t resourceIndex = 0; resourceIndex < m_resourceNames.size(); ++resourceIndex)
            {
                std::string label = std::format("{} {}", resourceIndex, Quote(m_resourceNames[resourceIndex]));
                if (m_resourceImported[resourceIndex] != 0 || m_resourceExported[resourceIndex] != 0)
                {
                    label += "\n";
                    if (m_resourceImported[resourceIndex] != 0)
                    {
                        label += "imported";
                    }
                    if (m_resourceExported[resourceIndex] != 0)
                    {
                        if (m_resourceImported[resourceIndex] != 0)
                        {
                            label += " ";
                        }
                        label += "exported";
                    }
                }
                if (m_lifetimesApplied)
                {
                    for (const ResourceLifetime& lifetime : m_resourceLifetimes)
                    {
                        if (lifetime.resourceIndex == resourceIndex)
                        {
                            label += std::format("\nfirst={} last={}", FormatPassIndex(lifetime.firstPass),
                                FormatPassIndex(lifetime.lastPass));
                            break;
                        }
                    }
                }
                dump += std::format("    r{} [label={}];\n", resourceIndex, DotQuote(label));
            }
            dump += "  }\n";
        }
        dump += "}\n";
        return dump;
    }
}
