// Fullscreen triangle for the S2.3 deferred lighting pass.
// UV origin is top-left (D3D / docs/renderer-conventions.md). clip.z = 0, clip.w = 1.
// Depth test is disabled; world position comes from GBufferDepth reconstruction.

void main(uint vertexId : SV_VertexID, out float4 clipPos : SV_Position, out float2 uv : TEXCOORD)
{
    uv = float2((vertexId << 1) & 2, vertexId & 2);
    clipPos = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}
