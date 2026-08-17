#include "gfx/d3d12/hresult_error.h"

#include <dxgi.h>

#include <catch2/catch_test_macros.hpp>

namespace
{
using renderlab::gfx::d3d12::ClassifyHResult;
using renderlab::gfx::d3d12::FormatHResultCode;
using renderlab::gfx::d3d12::FormatHResultDiagnostic;
using renderlab::gfx::d3d12::FormatHResultSystemMessage;
using renderlab::gfx::d3d12::HResultCategory;
using renderlab::gfx::d3d12::HResultDiagnosticData;
using renderlab::gfx::d3d12::HResultError;
using renderlab::gfx::d3d12::ThrowIfFailed;
} // namespace

TEST_CASE("HRESULT classification distinguishes success and common failures",
          "[hresult-error]")
{
    REQUIRE(ClassifyHResult(S_OK) == HResultCategory::success);
    REQUIRE(ClassifyHResult(S_FALSE) == HResultCategory::success);
    REQUIRE(ClassifyHResult(E_INVALIDARG) == HResultCategory::invalidArgument);
    REQUIRE(ClassifyHResult(E_OUTOFMEMORY) == HResultCategory::outOfMemory);
    REQUIRE(ClassifyHResult(E_NOINTERFACE) == HResultCategory::noInterface);
    REQUIRE(ClassifyHResult(E_FAIL) == HResultCategory::failure);
}

TEST_CASE("HRESULT classification preserves device removal causes",
          "[hresult-error]")
{
    REQUIRE(ClassifyHResult(DXGI_ERROR_DEVICE_REMOVED) ==
            HResultCategory::deviceRemoved);
    REQUIRE(ClassifyHResult(DXGI_ERROR_DEVICE_RESET) ==
            HResultCategory::deviceReset);
    REQUIRE(ClassifyHResult(DXGI_ERROR_DEVICE_HUNG) ==
            HResultCategory::deviceHung);
    REQUIRE(ClassifyHResult(DXGI_ERROR_DRIVER_INTERNAL_ERROR) ==
            HResultCategory::driverInternalError);
}

TEST_CASE("HRESULT code formatting is stable", "[hresult-error]")
{
    REQUIRE(FormatHResultCode(S_OK) == "0x00000000");
    REQUIRE(FormatHResultCode(S_FALSE) == "0x00000001");
    REQUIRE(FormatHResultCode(E_FAIL) == "0x80004005");
    REQUIRE(FormatHResultCode(DXGI_ERROR_DEVICE_REMOVED) == "0x887A0005");
}

TEST_CASE("HRESULT diagnostics preserve operation category and source",
          "[hresult-error]")
{
    const HResultDiagnosticData data{
        .status = DXGI_ERROR_DEVICE_REMOVED,
        .operation = "SyntheticOperation",
        .sourceFile = "synthetic.cpp",
        .sourceLine = 42,
        .functionName = "SyntheticFunction",
    };

    const auto diagnostic = FormatHResultDiagnostic(data);

    REQUIRE(diagnostic.find("operation=SyntheticOperation") != std::string::npos);
    REQUIRE(diagnostic.find("status=0x887A0005") != std::string::npos);
    REQUIRE(diagnostic.find("category=device_removed") != std::string::npos);
    REQUIRE(diagnostic.find("message=") != std::string::npos);
    REQUIRE(diagnostic.find("source=synthetic.cpp:42") != std::string::npos);
    REQUIRE(diagnostic.find("function=SyntheticFunction") != std::string::npos);
}

TEST_CASE("HRESULT system message always has explicit text", "[hresult-error]")
{
    REQUIRE_FALSE(FormatHResultSystemMessage(E_INVALIDARG).empty());
    REQUIRE_FALSE(
        FormatHResultSystemMessage(static_cast<HRESULT>(0x81234567)).empty());
}

TEST_CASE("ThrowIfFailed accepts successful HRESULT values", "[hresult-error]")
{
    REQUIRE_NOTHROW(ThrowIfFailed(S_OK, "S_OK operation"));
    REQUIRE_NOTHROW(ThrowIfFailed(S_FALSE, "S_FALSE operation"));
}

TEST_CASE("ThrowIfFailed throws a structured HRESULT error", "[hresult-error]")
{
    try
    {
        ThrowIfFailed(E_OUTOFMEMORY, "Synthetic allocation");
        FAIL("ThrowIfFailed should throw for a failed HRESULT");
    }
    catch (const HResultError &error)
    {
        REQUIRE(error.status() == E_OUTOFMEMORY);
        REQUIRE(error.operation() == "Synthetic allocation");
        REQUIRE(error.location().line() != 0);
        REQUIRE(std::string_view(error.what()).find("category=out_of_memory") !=
                std::string_view::npos);
    }
}
