#pragma once

#include "gfx/d3d12/adapter_discovery.h"

#include <string>

namespace renderlab::gfx::d3d12
{
[[nodiscard]] std::wstring FormatAdapterReport(
    const AdapterDiscoveryResult &discovery);
} // namespace renderlab::gfx::d3d12
