#include "gfx/d3d12/device_feature_report.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
using renderlab::gfx::d3d12::DeviceFeatureQueryResult;
using renderlab::gfx::d3d12::FeatureLevelName;
using renderlab::gfx::d3d12::FormatDeviceFeatureReport;
using renderlab::gfx::d3d12::RaytracingTierName;
using renderlab::gfx::d3d12::ResourceBindingTierName;
using renderlab::gfx::d3d12::RootSignatureVersionName;
using renderlab::gfx::d3d12::ShaderModelName;
} // namespace

TEST_CASE("feature level names cover the queried baseline",
          "[device-feature-report]")
{
    REQUIRE(FeatureLevelName(D3D_FEATURE_LEVEL_12_2) == "12_2");
    REQUIRE(FeatureLevelName(D3D_FEATURE_LEVEL_12_1) == "12_1");
    REQUIRE(FeatureLevelName(D3D_FEATURE_LEVEL_12_0) == "12_0");
    REQUIRE(FeatureLevelName(D3D_FEATURE_LEVEL_11_1) == "11_1");
    REQUIRE(FeatureLevelName(D3D_FEATURE_LEVEL_11_0) == "11_0");
}

TEST_CASE("unknown feature level has a stable name",
          "[device-feature-report]")
{
    REQUIRE(
        FeatureLevelName(
            static_cast<D3D_FEATURE_LEVEL>(0x7FFFFFFF)) ==
        "unknown");
}

TEST_CASE("shader model names cover the queried baseline",
          "[device-feature-report]")
{
    REQUIRE(ShaderModelName(D3D_SHADER_MODEL_6_10) == "6_10");
    REQUIRE(ShaderModelName(D3D_SHADER_MODEL_6_9) == "6_9");
    REQUIRE(ShaderModelName(D3D_SHADER_MODEL_6_8) == "6_8");
    REQUIRE(ShaderModelName(D3D_SHADER_MODEL_6_7) == "6_7");
    REQUIRE(ShaderModelName(D3D_SHADER_MODEL_6_6) == "6_6");
    REQUIRE(ShaderModelName(D3D_SHADER_MODEL_6_5) == "6_5");
    REQUIRE(ShaderModelName(D3D_SHADER_MODEL_6_4) == "6_4");
    REQUIRE(ShaderModelName(D3D_SHADER_MODEL_6_3) == "6_3");
    REQUIRE(ShaderModelName(D3D_SHADER_MODEL_6_2) == "6_2");
    REQUIRE(ShaderModelName(D3D_SHADER_MODEL_6_1) == "6_1");
    REQUIRE(ShaderModelName(D3D_SHADER_MODEL_6_0) == "6_0");
}

TEST_CASE("unknown shader model has a stable name",
          "[device-feature-report]")
{
    REQUIRE(
        ShaderModelName(
            static_cast<D3D_SHADER_MODEL>(0x7FFFFFFF)) ==
        "unknown");
}

TEST_CASE("resource binding tier names cover the queried baseline",
          "[device-feature-report]")
{
    REQUIRE(ResourceBindingTierName(D3D12_RESOURCE_BINDING_TIER_1) == "tier_1");
    REQUIRE(ResourceBindingTierName(D3D12_RESOURCE_BINDING_TIER_2) == "tier_2");
    REQUIRE(ResourceBindingTierName(D3D12_RESOURCE_BINDING_TIER_3) == "tier_3");
}

TEST_CASE("unknown resource binding tier has a stable name",
          "[device-feature-report]")
{
    REQUIRE(
        ResourceBindingTierName(
            static_cast<D3D12_RESOURCE_BINDING_TIER>(0x7FFFFFFF)) ==
        "unknown");
}

TEST_CASE("root signature version names cover the queried baseline",
          "[device-feature-report]")
{
    REQUIRE(RootSignatureVersionName(D3D_ROOT_SIGNATURE_VERSION_1_0) == "1_0");
    REQUIRE(RootSignatureVersionName(D3D_ROOT_SIGNATURE_VERSION_1_1) == "1_1");
}

TEST_CASE("unknown root signature version has a stable name",
          "[device-feature-report]")
{
    REQUIRE(
        RootSignatureVersionName(
            static_cast<D3D_ROOT_SIGNATURE_VERSION>(0x7FFFFFFF)) ==
        "unknown");
}

TEST_CASE("raytracing tier names cover supported and unsupported devices",
          "[device-feature-report]")
{
    REQUIRE(RaytracingTierName(D3D12_RAYTRACING_TIER_NOT_SUPPORTED) ==
            "not_supported");
    REQUIRE(RaytracingTierName(D3D12_RAYTRACING_TIER_1_0) == "tier_1_0");
    REQUIRE(RaytracingTierName(D3D12_RAYTRACING_TIER_1_1) == "tier_1_1");
}

TEST_CASE("unknown raytracing tier has a stable name",
          "[device-feature-report]")
{
    REQUIRE(
        RaytracingTierName(
            static_cast<D3D12_RAYTRACING_TIER>(0x7FFFFFFF)) ==
        "unknown");
}

TEST_CASE("device feature report describes a successful query",
          "[device-feature-report]")
{
    const DeviceFeatureQueryResult query{
        .status = S_OK,
        .features = {
            .actualFeatureLevel = D3D_FEATURE_LEVEL_12_2,
            .shaderModel = D3D_SHADER_MODEL_6_10,
            .resourceBindingTier = D3D12_RESOURCE_BINDING_TIER_3,
            .rootSignatureVersion = D3D_ROOT_SIGNATURE_VERSION_1_1,
            .raytracingTier = D3D12_RAYTRACING_TIER_1_1,
        },
        .featureLevelQueried = true,
        .shaderModelStatus = S_OK,
        .shaderModelQueried = true,
        .resourceBindingTierStatus = S_OK,
        .resourceBindingTierQueried = true,
        .rootSignatureVersionStatus = S_OK,
        .rootSignatureVersionQueried = true,
        .raytracingTierStatus = S_OK,
        .raytracingTierQueried = true,
    };

    const auto report = FormatDeviceFeatureReport(query);

    REQUIRE(report.find("status=0x00000000") != std::string::npos);
    REQUIRE(report.find("actual_feature_level=12_2") != std::string::npos);
    REQUIRE(report.find("shader_model_query_status=0x00000000") != std::string::npos);
    REQUIRE(report.find("shader_model=6_10") != std::string::npos);
    REQUIRE(report.find("resource_binding_tier_query_status=0x00000000") !=
            std::string::npos);
    REQUIRE(report.find("resource_binding_tier=tier_3") != std::string::npos);
    REQUIRE(report.find("root_signature_version_query_status=0x00000000") !=
            std::string::npos);
    REQUIRE(report.find("root_signature_version=1_1") != std::string::npos);
    REQUIRE(report.find("raytracing_tier_query_status=0x00000000") !=
            std::string::npos);
    REQUIRE(report.find("raytracing_tier=tier_1_1") != std::string::npos);
}

TEST_CASE("device feature report does not expose default data after failure",
          "[device-feature-report]")
{
    const DeviceFeatureQueryResult query{
        .status = E_FAIL,
        .featureLevelQueried = false,
    };

    const auto report = FormatDeviceFeatureReport(query);

    REQUIRE(report.find("status=0x80004005") != std::string::npos);
    REQUIRE(report.find("actual_feature_level=unavailable") != std::string::npos);
    REQUIRE(report.find("shader_model_query_status=0x80004005") != std::string::npos);
    REQUIRE(report.find("shader_model=unavailable") != std::string::npos);
    REQUIRE(report.find("resource_binding_tier_query_status=0x80004005") !=
            std::string::npos);
    REQUIRE(report.find("resource_binding_tier=unavailable") != std::string::npos);
    REQUIRE(report.find("root_signature_version_query_status=0x80004005") !=
            std::string::npos);
    REQUIRE(report.find("root_signature_version=unavailable") != std::string::npos);
    REQUIRE(report.find("raytracing_tier_query_status=0x80004005") !=
            std::string::npos);
    REQUIRE(report.find("raytracing_tier=unavailable") != std::string::npos);
}

TEST_CASE("device feature report preserves a shader model query failure",
          "[device-feature-report]")
{
    const DeviceFeatureQueryResult query{
        .status = E_INVALIDARG,
        .features = {
            .actualFeatureLevel = D3D_FEATURE_LEVEL_12_1,
        },
        .featureLevelQueried = true,
        .shaderModelStatus = E_INVALIDARG,
        .shaderModelQueried = false,
    };

    const auto report = FormatDeviceFeatureReport(query);

    REQUIRE(report.find("status=0x80070057") != std::string::npos);
    REQUIRE(report.find("actual_feature_level=12_1") != std::string::npos);
    REQUIRE(report.find("shader_model_query_status=0x80070057") != std::string::npos);
    REQUIRE(report.find("shader_model=unavailable") != std::string::npos);
    REQUIRE(report.find("resource_binding_tier=unavailable") != std::string::npos);
    REQUIRE(report.find("root_signature_version=unavailable") != std::string::npos);
    REQUIRE(report.find("raytracing_tier=unavailable") != std::string::npos);
}

TEST_CASE("device feature report preserves a resource binding tier query failure",
          "[device-feature-report]")
{
    const DeviceFeatureQueryResult query{
        .status = E_OUTOFMEMORY,
        .features = {
            .actualFeatureLevel = D3D_FEATURE_LEVEL_12_2,
            .shaderModel = D3D_SHADER_MODEL_6_8,
        },
        .featureLevelQueried = true,
        .shaderModelStatus = S_OK,
        .shaderModelQueried = true,
        .resourceBindingTierStatus = E_OUTOFMEMORY,
        .resourceBindingTierQueried = false,
    };

    const auto report = FormatDeviceFeatureReport(query);

    REQUIRE(report.find("status=0x8007000E") != std::string::npos);
    REQUIRE(report.find("actual_feature_level=12_2") != std::string::npos);
    REQUIRE(report.find("shader_model=6_8") != std::string::npos);
    REQUIRE(report.find("resource_binding_tier_query_status=0x8007000E") !=
            std::string::npos);
    REQUIRE(report.find("resource_binding_tier=unavailable") != std::string::npos);
    REQUIRE(report.find("root_signature_version=unavailable") != std::string::npos);
    REQUIRE(report.find("raytracing_tier=unavailable") != std::string::npos);
}

TEST_CASE("device feature report preserves a root signature query failure",
          "[device-feature-report]")
{
    const DeviceFeatureQueryResult query{
        .status = E_INVALIDARG,
        .features = {
            .actualFeatureLevel = D3D_FEATURE_LEVEL_12_2,
            .shaderModel = D3D_SHADER_MODEL_6_8,
            .resourceBindingTier = D3D12_RESOURCE_BINDING_TIER_3,
        },
        .featureLevelQueried = true,
        .shaderModelStatus = S_OK,
        .shaderModelQueried = true,
        .resourceBindingTierStatus = S_OK,
        .resourceBindingTierQueried = true,
        .rootSignatureVersionStatus = E_INVALIDARG,
        .rootSignatureVersionQueried = false,
    };

    const auto report = FormatDeviceFeatureReport(query);

    REQUIRE(report.find("status=0x80070057") != std::string::npos);
    REQUIRE(report.find("actual_feature_level=12_2") != std::string::npos);
    REQUIRE(report.find("shader_model=6_8") != std::string::npos);
    REQUIRE(report.find("resource_binding_tier=tier_3") != std::string::npos);
    REQUIRE(report.find("root_signature_version_query_status=0x80070057") !=
            std::string::npos);
    REQUIRE(report.find("root_signature_version=unavailable") != std::string::npos);
    REQUIRE(report.find("raytracing_tier=unavailable") != std::string::npos);
}

TEST_CASE("device feature report treats unsupported raytracing as valid data",
          "[device-feature-report]")
{
    const DeviceFeatureQueryResult query{
        .status = S_OK,
        .features = {
            .actualFeatureLevel = D3D_FEATURE_LEVEL_12_0,
            .shaderModel = D3D_SHADER_MODEL_6_6,
            .resourceBindingTier = D3D12_RESOURCE_BINDING_TIER_2,
            .rootSignatureVersion = D3D_ROOT_SIGNATURE_VERSION_1_1,
            .raytracingTier = D3D12_RAYTRACING_TIER_NOT_SUPPORTED,
        },
        .featureLevelQueried = true,
        .shaderModelStatus = S_OK,
        .shaderModelQueried = true,
        .resourceBindingTierStatus = S_OK,
        .resourceBindingTierQueried = true,
        .rootSignatureVersionStatus = S_OK,
        .rootSignatureVersionQueried = true,
        .raytracingTierStatus = S_OK,
        .raytracingTierQueried = true,
    };

    const auto report = FormatDeviceFeatureReport(query);

    REQUIRE(report.find("status=0x00000000") != std::string::npos);
    REQUIRE(report.find("raytracing_tier_query_status=0x00000000") !=
            std::string::npos);
    REQUIRE(report.find("raytracing_tier=not_supported") != std::string::npos);
}

TEST_CASE("device feature report allows an unavailable raytracing query",
          "[device-feature-report]")
{
    const DeviceFeatureQueryResult query{
        .status = S_OK,
        .features = {
            .actualFeatureLevel = D3D_FEATURE_LEVEL_12_0,
            .shaderModel = D3D_SHADER_MODEL_6_6,
            .resourceBindingTier = D3D12_RESOURCE_BINDING_TIER_3,
            .rootSignatureVersion = D3D_ROOT_SIGNATURE_VERSION_1_1,
        },
        .featureLevelQueried = true,
        .shaderModelStatus = S_OK,
        .shaderModelQueried = true,
        .resourceBindingTierStatus = S_OK,
        .resourceBindingTierQueried = true,
        .rootSignatureVersionStatus = S_OK,
        .rootSignatureVersionQueried = true,
        .raytracingTierStatus = E_INVALIDARG,
        .raytracingTierQueried = false,
    };

    const auto report = FormatDeviceFeatureReport(query);

    REQUIRE(report.find("status=0x00000000") != std::string::npos);
    REQUIRE(report.find("raytracing_tier_query_status=0x80070057") !=
            std::string::npos);
    REQUIRE(report.find("raytracing_tier=unavailable") != std::string::npos);
}

TEST_CASE("device feature report preserves a raytracing query failure",
          "[device-feature-report]")
{
    const DeviceFeatureQueryResult query{
        .status = E_OUTOFMEMORY,
        .features = {
            .actualFeatureLevel = D3D_FEATURE_LEVEL_12_2,
            .shaderModel = D3D_SHADER_MODEL_6_8,
            .resourceBindingTier = D3D12_RESOURCE_BINDING_TIER_3,
            .rootSignatureVersion = D3D_ROOT_SIGNATURE_VERSION_1_1,
        },
        .featureLevelQueried = true,
        .shaderModelStatus = S_OK,
        .shaderModelQueried = true,
        .resourceBindingTierStatus = S_OK,
        .resourceBindingTierQueried = true,
        .rootSignatureVersionStatus = S_OK,
        .rootSignatureVersionQueried = true,
        .raytracingTierStatus = E_OUTOFMEMORY,
        .raytracingTierQueried = false,
    };

    const auto report = FormatDeviceFeatureReport(query);

    REQUIRE(report.find("status=0x8007000E") != std::string::npos);
    REQUIRE(report.find("root_signature_version=1_1") != std::string::npos);
    REQUIRE(report.find("raytracing_tier_query_status=0x8007000E") !=
            std::string::npos);
    REQUIRE(report.find("raytracing_tier=unavailable") != std::string::npos);
}
