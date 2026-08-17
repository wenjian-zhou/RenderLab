#pragma once

#include <d3d12.h>

namespace renderlab::gfx::d3d12
{
struct DeviceFeatures
{
    D3D_FEATURE_LEVEL actualFeatureLevel =
        D3D_FEATURE_LEVEL_11_0;
    D3D_SHADER_MODEL shaderModel =
        D3D_SHADER_MODEL_6_0;
    D3D12_RESOURCE_BINDING_TIER resourceBindingTier =
        D3D12_RESOURCE_BINDING_TIER_1;
    D3D_ROOT_SIGNATURE_VERSION rootSignatureVersion =
        D3D_ROOT_SIGNATURE_VERSION_1_0;
    D3D12_RAYTRACING_TIER raytracingTier =
        D3D12_RAYTRACING_TIER_NOT_SUPPORTED;
};

struct DeviceFeatureQueryResult
{
    HRESULT status = E_FAIL;
    DeviceFeatures features{};
    bool featureLevelQueried = false;
    HRESULT shaderModelStatus = E_FAIL;
    bool shaderModelQueried = false;
    HRESULT resourceBindingTierStatus = E_FAIL;
    bool resourceBindingTierQueried = false;
    HRESULT rootSignatureVersionStatus = E_FAIL;
    bool rootSignatureVersionQueried = false;
    HRESULT raytracingTierStatus = E_FAIL;
    bool raytracingTierQueried = false;
};

[[nodiscard]] DeviceFeatureQueryResult QueryDeviceFeatures(
    ID3D12Device &device) noexcept;
} // namespace renderlab::gfx::d3d12
