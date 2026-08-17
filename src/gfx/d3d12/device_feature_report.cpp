#include "gfx/d3d12/device_feature_report.h"
#include "gfx/d3d12/hresult_error.h"

namespace renderlab::gfx::d3d12
{
std::string_view FeatureLevelName(
    D3D_FEATURE_LEVEL featureLevel) noexcept
{
    switch (featureLevel)
    {
    case D3D_FEATURE_LEVEL_12_2:
        return "12_2";
    case D3D_FEATURE_LEVEL_12_1:
        return "12_1";
    case D3D_FEATURE_LEVEL_12_0:
        return "12_0";
    case D3D_FEATURE_LEVEL_11_1:
        return "11_1";
    case D3D_FEATURE_LEVEL_11_0:
        return "11_0";
    default:
        return "unknown";
    }
}

std::string_view ShaderModelName(
    D3D_SHADER_MODEL shaderModel) noexcept
{
    switch (shaderModel)
    {
    case D3D_SHADER_MODEL_6_10:
        return "6_10";
    case D3D_SHADER_MODEL_6_9:
        return "6_9";
    case D3D_SHADER_MODEL_6_8:
        return "6_8";
    case D3D_SHADER_MODEL_6_7:
        return "6_7";
    case D3D_SHADER_MODEL_6_6:
        return "6_6";
    case D3D_SHADER_MODEL_6_5:
        return "6_5";
    case D3D_SHADER_MODEL_6_4:
        return "6_4";
    case D3D_SHADER_MODEL_6_3:
        return "6_3";
    case D3D_SHADER_MODEL_6_2:
        return "6_2";
    case D3D_SHADER_MODEL_6_1:
        return "6_1";
    case D3D_SHADER_MODEL_6_0:
        return "6_0";
    default:
        return "unknown";
    }
}

std::string_view ResourceBindingTierName(
    D3D12_RESOURCE_BINDING_TIER tier) noexcept
{
    switch (tier)
    {
    case D3D12_RESOURCE_BINDING_TIER_1:
        return "tier_1";
    case D3D12_RESOURCE_BINDING_TIER_2:
        return "tier_2";
    case D3D12_RESOURCE_BINDING_TIER_3:
        return "tier_3";
    default:
        return "unknown";
    }
}

std::string_view RootSignatureVersionName(
    D3D_ROOT_SIGNATURE_VERSION version) noexcept
{
    switch (version)
    {
    case D3D_ROOT_SIGNATURE_VERSION_1_0:
        return "1_0";
    case D3D_ROOT_SIGNATURE_VERSION_1_1:
        return "1_1";
    default:
        return "unknown";
    }
}

std::string_view RaytracingTierName(
    D3D12_RAYTRACING_TIER tier) noexcept
{
    switch (tier)
    {
    case D3D12_RAYTRACING_TIER_NOT_SUPPORTED:
        return "not_supported";
    case D3D12_RAYTRACING_TIER_1_0:
        return "tier_1_0";
    case D3D12_RAYTRACING_TIER_1_1:
        return "tier_1_1";
    default:
        return "unknown";
    }
}

std::string FormatDeviceFeatureReport(
    const DeviceFeatureQueryResult &query)
{
    std::string report = "D3D12 device features: status=";
    report += FormatHResultCode(query.status);
    report += "; actual_feature_level=";

    if (query.featureLevelQueried)
    {
        report += FeatureLevelName(
            query.features.actualFeatureLevel);
    }
    else
    {
        report += "unavailable";
    }

    report += "; shader_model_query_status=";
    report += FormatHResultCode(query.shaderModelStatus);
    report += "; shader_model=";

    if (query.shaderModelQueried)
    {
        report += ShaderModelName(
            query.features.shaderModel);
    }
    else
    {
        report += "unavailable";
    }

    report += "; resource_binding_tier_query_status=";
    report += FormatHResultCode(query.resourceBindingTierStatus);
    report += "; resource_binding_tier=";

    if (query.resourceBindingTierQueried)
    {
        report += ResourceBindingTierName(
            query.features.resourceBindingTier);
    }
    else
    {
        report += "unavailable";
    }

    report += "; root_signature_version_query_status=";
    report += FormatHResultCode(query.rootSignatureVersionStatus);
    report += "; root_signature_version=";

    if (query.rootSignatureVersionQueried)
    {
        report += RootSignatureVersionName(
            query.features.rootSignatureVersion);
    }
    else
    {
        report += "unavailable";
    }

    report += "; raytracing_tier_query_status=";
    report += FormatHResultCode(query.raytracingTierStatus);
    report += "; raytracing_tier=";

    if (query.raytracingTierQueried)
    {
        report += RaytracingTierName(
            query.features.raytracingTier);
    }
    else
    {
        report += "unavailable";
    }

    return report;
}
} // namespace renderlab::gfx::d3d12
