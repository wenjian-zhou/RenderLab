#include "GraphCompiler.h"

#include <algorithm>
#include <cassert>
#include <format>
#include <string_view>
#include <tuple>
#include <utility>

namespace renderlab::rdg
{
    namespace
    {
        const char* ToString(ErrorCategory category)
        {
            switch (category)
            {
            case ErrorCategory::NullHandle: return "NullHandle";
            case ErrorCategory::ForeignGraph: return "ForeignGraph";
            case ErrorCategory::TypeMismatch: return "TypeMismatch";
            case ErrorCategory::InvalidName: return "InvalidName";
            case ErrorCategory::InvalidDescriptor: return "InvalidDescriptor";
            case ErrorCategory::InvalidPassFlags: return "InvalidPassFlags";
            case ErrorCategory::ReadBeforeProduce: return "ReadBeforeProduce";
            case ErrorCategory::DuplicateWrite: return "DuplicateWrite";
            case ErrorCategory::IncompatibleAccess: return "IncompatibleAccess";
            case ErrorCategory::ZeroUseAllocation: return "ZeroUseAllocation";
            case ErrorCategory::UndeclaredAccess: return "UndeclaredAccess";
            case ErrorCategory::ExpiredContext: return "ExpiredContext";
            case ErrorCategory::UnregisteredImport: return "UnregisteredImport";
            case ErrorCategory::InvalidPass: return "InvalidPass";
            case ErrorCategory::AllocationFailed: return "AllocationFailed";
            case ErrorCategory::UnknownAccess: return "UnknownAccess";
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

        bool WritesCullRoot(const GraphBuilder& builder, uint32_t passIndex)
        {
            for (const ResourceAccess& access : builder.GetPass(passIndex).accesses)
            {
                if (!IsWritableAccess(access.access))
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
        struct ResourceUse
        {
            bool seen = false;
            uint32_t firstPass = Error::kNoPass;
            uint32_t lastPass = Error::kNoPass;
        };

        const uint32_t resourceCount = static_cast<uint32_t>(builder.GetResourceCount());
        std::vector<ResourceUse> uses(resourceCount);

        for (uint32_t slot = 0; slot < result.m_livePassOrder.size(); ++slot)
        {
            const uint32_t passIndex = result.m_livePassOrder[slot];
            for (const ResourceAccess& access : builder.GetPass(passIndex).accesses)
            {
                ResourceUse& use = uses[access.index];
                if (!use.seen)
                {
                    use.seen = true;
                    use.firstPass = passIndex;
                }
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
            if (resource.imported && !uses[resourceIndex].seen)
            {
                ResourceUse& unusedImport = uses[resourceIndex];
                unusedImport.seen = true;
                unusedImport.firstPass = firstLivePass;
                unusedImport.lastPass = firstLivePass;
            }
            if (!uses[resourceIndex].seen && !resource.imported)
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
            const ResourceUse& use = uses[resourceIndex];
            if (!use.seen)
            {
                continue;
            }
            uint32_t minPass = use.firstPass;
            uint32_t maxPass = use.lastPass;
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
        }
        result.m_errors.assign(builder.GetErrors().begin(), builder.GetErrors().end());
        if (!result.m_errors.empty())
        {
            return result;
        }

        const uint32_t passCount = static_cast<uint32_t>(builder.GetPassCount());
        const uint32_t resourceCount = static_cast<uint32_t>(builder.GetResourceCount());
        std::vector<uint32_t> lastWriter(resourceCount, Error::kNoPass);
        struct PendingLink
        {
            uint32_t fromPass = 0;
            uint32_t toPass = 0;
            EdgeResource resource;
        };
        std::vector<PendingLink> links;
        for (uint32_t passIndex = 0; passIndex < passCount; ++passIndex)
        {
            for (const ResourceAccess& access : builder.GetPass(passIndex).accesses)
            {
                if (IsWritableAccess(access.access))
                {
                    lastWriter[access.index] = passIndex;
                    continue;
                }
                if (access.access != Access::ShaderResource)
                {
                    continue;
                }
                const uint32_t producer = lastWriter[access.index];
                if (producer == Error::kNoPass || producer == passIndex)
                {
                    continue;
                }
                links.push_back(PendingLink{
                    producer,
                    passIndex,
                    EdgeResource{access.kind, access.index}});
            }
        }
        std::sort(links.begin(), links.end(), [](const PendingLink& left, const PendingLink& right)
        {
            const uint32_t leftKind = static_cast<uint32_t>(left.resource.kind);
            const uint32_t rightKind = static_cast<uint32_t>(right.resource.kind);
            return std::tie(left.toPass, left.fromPass, left.resource.resourceIndex, leftKind) <
                std::tie(right.toPass, right.fromPass, right.resource.resourceIndex, rightKind);
        });
        for (const PendingLink& link : links)
        {
            if (!result.m_edges.empty() && result.m_edges.back().fromPass == link.fromPass &&
                result.m_edges.back().toPass == link.toPass)
            {
                std::vector<EdgeResource>& resources = result.m_edges.back().resources;
                if (resources.empty() || resources.back().resourceIndex != link.resource.resourceIndex)
                {
                    resources.push_back(link.resource);
                }
                continue;
            }
            result.m_edges.push_back(DependencyEdge{link.fromPass, link.toPass, {link.resource}});
        }

        result.m_passOrder.resize(passCount);
        for (uint32_t passIndex = 0; passIndex < passCount; ++passIndex)
        {
            result.m_passOrder[passIndex] = passIndex;
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
            for (const EdgeResource& resource : edge.resources)
            {
                dump += std::format("    {} {} {}\n", ToString(resource.kind), resource.resourceIndex,
                    Quote(m_resourceNames[resource.resourceIndex]));
            }
        }
        dump += "errors:\n";
        for (const Error& error : m_errors)
        {
            dump += std::format("  {} pass={} {} resource={} {}\n", ToString(error.category),
                error.passIndex == Error::kNoPass ? "none" : std::to_string(error.passIndex),
                Quote(error.passName), Quote(error.resourceName), Quote(error.message));
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
                dump += std::format("    {} {} {} {}\n", ToString(access.access), ToString(access.kind),
                    access.index, Quote(m_resourceNames[access.index]));
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
            std::string label;
            for (const EdgeResource& resource : edge.resources)
            {
                if (!label.empty())
                {
                    label += "\n";
                }
                label += std::format("{} {} {}", ToString(resource.kind), resource.resourceIndex,
                    Quote(m_resourceNames[resource.resourceIndex]));
            }
            dump += std::format("  p{} -> p{} [label={}];\n", edge.fromPass, edge.toPass, DotQuote(label));
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
