// Fullscreen triangle for GBuffer debug visualization.
// UV origin is top-left (D3D / docs/renderer-conventions.md). clip.z = 0, clip.w = 1.
// Depth test is disabled; this is not world-position reconstruction.

void main(uint vertexId : SV_VertexID, out float4 clipPos : SV_Position, out float2 uv : TEXCOORD)
{
    uv = float2((vertexId << 1) & 2, vertexId & 2);
    clipPos = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}
