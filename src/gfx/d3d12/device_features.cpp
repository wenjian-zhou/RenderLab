#include "gfx/d3d12/device_features.h"

#include <array>

namespace renderlab::gfx::d3d12
{
DeviceFeatureQueryResult QueryDeviceFeatures(
    ID3D12Device &device) noexcept
{
    DeviceFeatureQueryResult result{};

    constexpr std::array requestedFeatureLevels{
        D3D_FEATURE_LEVEL_12_2,
        D3D_FEATURE_LEVEL_12_1,
        D3D_FEATURE_LEVEL_12_0,
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
    };

    D3D12_FEATURE_DATA_FEATURE_LEVELS query{
        .NumFeatureLevels =
            static_cast<UINT>(requestedFeatureLevels.size()),
        .pFeatureLevelsRequested =
            requestedFeatureLevels.data(),
        .MaxSupportedFeatureLevel =
            D3D_FEATURE_LEVEL_11_0,
    };

    result.status = device.CheckFeatureSupport(
        D3D12_FEATURE_FEATURE_LEVELS,
        &query,
        sizeof(query));

    if (FAILED(result.status))
    {
        return result;
    }

    result.features.actualFeatureLevel =
        query.MaxSupportedFeatureLevel;
    result.featureLevelQueried = true;

    constexpr std::array requestedShaderModels{
        D3D_SHADER_MODEL_6_10,
        D3D_SHADER_MODEL_6_9,
        D3D_SHADER_MODEL_6_8,
        D3D_SHADER_MODEL_6_7,
        D3D_SHADER_MODEL_6_6,
        D3D_SHADER_MODEL_6_5,
        D3D_SHADER_MODEL_6_4,
        D3D_SHADER_MODEL_6_3,
        D3D_SHADER_MODEL_6_2,
        D3D_SHADER_MODEL_6_1,
        D3D_SHADER_MODEL_6_0,
    };

    for (const D3D_SHADER_MODEL requestedShaderModel : requestedShaderModels)
    {
        D3D12_FEATURE_DATA_SHADER_MODEL shaderModelQuery{
            .HighestShaderModel = requestedShaderModel,
        };

        result.shaderModelStatus = device.CheckFeatureSupport(
            D3D12_FEATURE_SHADER_MODEL,
            &shaderModelQuery,
            sizeof(shaderModelQuery));

        if (result.shaderModelStatus == E_INVALIDARG)
        {
            continue;
        }

        if (FAILED(result.shaderModelStatus))
        {
            result.status = result.shaderModelStatus;
            return result;
        }

        result.features.shaderModel =
            shaderModelQuery.HighestShaderModel;
        result.shaderModelQueried = true;
        break;
    }

    if (!result.shaderModelQueried)
    {
        result.status = result.shaderModelStatus;
        return result;
    }

    D3D12_FEATURE_DATA_D3D12_OPTIONS options{};

    result.resourceBindingTierStatus = device.CheckFeatureSupport(
        D3D12_FEATURE_D3D12_OPTIONS,
        &options,
        sizeof(options));

    if (FAILED(result.resourceBindingTierStatus))
    {
        result.status = result.resourceBindingTierStatus;
        return result;
    }

    result.features.resourceBindingTier =
        options.ResourceBindingTier;
    result.resourceBindingTierQueried = true;

    D3D12_FEATURE_DATA_ROOT_SIGNATURE rootSignatureQuery{
        .HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1,
    };

    result.rootSignatureVersionStatus = device.CheckFeatureSupport(
        D3D12_FEATURE_ROOT_SIGNATURE,
        &rootSignatureQuery,
        sizeof(rootSignatureQuery));

    if (result.rootSignatureVersionStatus == E_INVALIDARG)
    {
        rootSignatureQuery.HighestVersion =
            D3D_ROOT_SIGNATURE_VERSION_1_0;

        result.rootSignatureVersionStatus = device.CheckFeatureSupport(
            D3D12_FEATURE_ROOT_SIGNATURE,
            &rootSignatureQuery,
            sizeof(rootSignatureQuery));
    }

    if (FAILED(result.rootSignatureVersionStatus))
    {
        result.status = result.rootSignatureVersionStatus;
        return result;
    }

    result.features.rootSignatureVersion =
        rootSignatureQuery.HighestVersion;
    result.rootSignatureVersionQueried = true;

    D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5{};

    result.raytracingTierStatus = device.CheckFeatureSupport(
        D3D12_FEATURE_D3D12_OPTIONS5,
        &options5,
        sizeof(options5));

    if (SUCCEEDED(result.raytracingTierStatus))
    {
        result.features.raytracingTier =
            options5.RaytracingTier;
        result.raytracingTierQueried = true;
    }
    else if (result.raytracingTierStatus != E_INVALIDARG)
    {
        result.status = result.raytracingTierStatus;
        return result;
    }

    result.status = S_OK;
    return result;
}
} // namespace renderlab::gfx::d3d12
