#pragma once

#include "gfx/d3d12/device_features.h"

#include <string>
#include <string_view>

namespace renderlab::gfx::d3d12
{
[[nodiscard]] std::string_view FeatureLevelName(
    D3D_FEATURE_LEVEL featureLevel) noexcept;

[[nodiscard]] std::string_view ShaderModelName(
    D3D_SHADER_MODEL shaderModel) noexcept;

[[nodiscard]] std::string_view ResourceBindingTierName(
    D3D12_RESOURCE_BINDING_TIER tier) noexcept;

[[nodiscard]] std::string_view RootSignatureVersionName(
    D3D_ROOT_SIGNATURE_VERSION version) noexcept;

[[nodiscard]] std::string_view RaytracingTierName(
    D3D12_RAYTRACING_TIER tier) noexcept;

[[nodiscard]] std::string FormatDeviceFeatureReport(
    const DeviceFeatureQueryResult &query);
} // namespace renderlab::gfx::d3d12
