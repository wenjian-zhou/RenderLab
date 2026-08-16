#include "gfx/d3d12/adapter_selection.h"

#include <catch2/catch_test_macros.hpp>

#include <array>

namespace
{
using renderlab::gfx::d3d12::AdapterCandidate;
using renderlab::gfx::d3d12::SelectHardwareAdapter;
}

TEST_CASE("empty adapter list has no selection", "[adapter-selection]")
{
    const std::array<AdapterCandidate, 0> candidates = {};

    const auto selected = SelectHardwareAdapter(candidates);

    REQUIRE_FALSE(selected.has_value());
}

TEST_CASE("software adapters are rejected", "[adapter-selection]")
{
    const std::array candidates = {
        AdapterCandidate{.isSoftware = true, .supportsD3D12 = true},
    };

    const auto selected = SelectHardwareAdapter(candidates);

    REQUIRE_FALSE(selected.has_value());
}

TEST_CASE("unsupported hardware adapters are skipped", "[adapter-selection]")
{
    const std::array candidates = {
        AdapterCandidate{.isSoftware = false, .supportsD3D12 = false},
        AdapterCandidate{.isSoftware = false, .supportsD3D12 = true},
    };

    const auto selected = SelectHardwareAdapter(candidates);

    REQUIRE(selected.has_value());
    REQUIRE(*selected == 1);
}

TEST_CASE("a valid hardware adapter after software is selected", "[adapter-selection]")
{
    const std::array candidates = {
        AdapterCandidate{.isSoftware = true, .supportsD3D12 = true},
        AdapterCandidate{.isSoftware = false, .supportsD3D12 = true},
    };

    const auto selected = SelectHardwareAdapter(candidates);

    REQUIRE(selected.has_value());
    REQUIRE(*selected == 1);
}

TEST_CASE("DXGI preference order is preserved", "[adapter-selection]")
{
    const std::array candidates = {
        AdapterCandidate{.isSoftware = false, .supportsD3D12 = true},
        AdapterCandidate{.isSoftware = false, .supportsD3D12 = true},
    };

    const auto selected = SelectHardwareAdapter(candidates);

    REQUIRE(selected.has_value());
    REQUIRE(*selected == 0);
}
