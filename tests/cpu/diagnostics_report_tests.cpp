#include "gfx/d3d12/diagnostics_report.h"

#include <catch2/catch_test_macros.hpp>

using renderlab::gfx::d3d12::DiagnosticsConfig;
using renderlab::gfx::d3d12::DiagnosticsState;
using renderlab::gfx::d3d12::FormatDiagnosticsReport;

TEST_CASE("diagnostics report records successful requested features", "[diagnostics]")
{
    const DiagnosticsConfig config{
        .enableDebugLayer = true,
        .enableGpuBasedValidation = true,
        .enableDred = true,
    };

    const DiagnosticsState state{
        .status = S_OK,
        .debugLayerEnabled = true,
        .gpuBasedValidationEnabled = true,
        .dredConfigured = true,
    };

    const auto report = FormatDiagnosticsReport(config, state);

    REQUIRE(report.find("status=0x00000000") != std::string::npos);
    REQUIRE(report.find("debug_layer=requested,enabled") != std::string::npos);
    REQUIRE(report.find("gpu_based_validation=requested,enabled") != std::string::npos);
    REQUIRE(report.find("dred=requested,configured") != std::string::npos);
    REQUIRE(report.find("info_queue=not-created") != std::string::npos);
}

TEST_CASE("diagnostics report distinguishes disabled features", "[diagnostics]")
{
    const DiagnosticsConfig config{
        .enableDebugLayer = false,
        .enableGpuBasedValidation = false,
        .enableDred = false,
    };

    const DiagnosticsState state{};

    const auto report = FormatDiagnosticsReport(config, state);

    REQUIRE(report.find("debug_layer=not-requested,not-enabled") != std::string::npos);
    REQUIRE(report.find("gpu_based_validation=not-requested,not-enabled") != std::string::npos);
    REQUIRE(report.find("dred=not-requested,not-configured") != std::string::npos);
    REQUIRE(report.find("info_queue=not-created") != std::string::npos);
}

TEST_CASE("diagnostics report preserves a failure status", "[diagnostics]")
{
    DiagnosticsConfig config{};
    DiagnosticsState state{};
    state.status = E_FAIL;

    const auto report = FormatDiagnosticsReport(config, state);

    REQUIRE(report.find("status=0x80004005") != std::string::npos);
}
