#include "renderer_cb.h"
#include "gbuffer_pass.hlsli"

// Opaque GBuffer vertex shader. Row-vector algebra: p_out = mul(p_in, M).
// Do not include Donut gbuffer_cb.h, forward_vertex.hlsli, or PlanarViewConstants.

ConstantBuffer<ViewConstants>     g_View     : register(b1);
ConstantBuffer<InstanceConstants> g_Instance : register(b2);

GBufferVSOutput main(GBufferVSInput input)
{
    GBufferVSOutput output;

    const float4 worldPos = mul(float4(input.position, 1.0), g_Instance.matLocalToWorld);
    output.clipPosition = mul(worldPos, g_View.matWorldToClip);
    output.texCoord = input.texCoord;

    const float3x3 worldToLocal3 = (float3x3)g_Instance.matWorldToLocal;
    output.worldNormal = normalize(mul(input.normal.xyz, transpose(worldToLocal3)));
    output.worldTangent.xyz = normalize(mul(input.tangent.xyz, transpose(worldToLocal3)));
    output.worldTangent.w = input.tangent.w;

    return output;
}
