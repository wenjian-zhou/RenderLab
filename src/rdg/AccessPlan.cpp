#include "AccessPlan.h"

#include <format>
#include <string_view>

namespace renderlab::rdg
{
    namespace
    {
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
    }

    std::string AccessPlan::Dump() const
    {
        if (resources.empty())
        {
            return "access-plan: empty\n";
        }

        std::string dump = "access-plan:\n";
        for (const ResourceBoundary& resource : resources)
        {
            dump += std::format("  resource {} {}", resource.resourceIndex, Quote(resource.name));
            if (resource.imported)
            {
                dump += " imported";
            }
            if (resource.exported)
            {
                dump += " exported";
            }
            dump += std::format(" initial={} final={}\n", ToString(resource.initial), ToString(resource.final));
        }

        uint32_t lastPass = 0xFFFFFFFFu;
        for (const PassResourceState& state : passes)
        {
            if (state.passIndex != lastPass)
            {
                dump += std::format("  pass {} {}\n", state.passIndex, Quote(state.passName));
                lastPass = state.passIndex;
            }
            dump += std::format(
                "    {} {} {} {} before={} required={} after={}\n",
                ToString(state.required),
                ToString(state.kind),
                state.resourceIndex,
                Quote(state.resourceName),
                ToString(state.before),
                ToString(state.required),
                ToString(state.after));
        }

        if (restores.empty())
        {
            dump += "  restore: none\n";
        }
        else
        {
            dump += "  restore:\n";
            for (const ResourceRestore& restore : restores)
            {
                dump += std::format(
                    "    {} {} {} -> {}\n",
                    restore.resourceIndex,
                    Quote(restore.name),
                    ToString(restore.from),
                    ToString(restore.to));
            }
        }
        return dump;
    }
}
