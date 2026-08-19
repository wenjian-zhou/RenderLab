#pragma once

#include "GBufferContract.h"
#include "shaders/renderer_cb.h"

#include <donut/core/math/math.h>
#include <donut/engine/SceneTypes.h>
#include <nvrhi/nvrhi.h>

#include <cstdint>
#include <string>
#include <vector>

namespace donut::engine
{
    class SceneGraph;
    class MeshInstance;
}

namespace renderlab
{
    inline constexpr uint32_t kFrameConstantsByteSize = 16;
    inline constexpr uint32_t kViewConstantsByteSize = 448;
    inline constexpr uint32_t kInstanceConstantsByteSize = 128;
    inline constexpr uint32_t kMaterialParamsByteSize = 32;

    static_assert(sizeof(FrameConstants) == kFrameConstantsByteSize);
    static_assert(sizeof(ViewConstants) == kViewConstantsByteSize);
    static_assert(sizeof(InstanceConstants) == kInstanceConstantsByteSize);
    static_assert(sizeof(MaterialParams) == kMaterialParamsByteSize);
    static_assert(kFrameConstantsByteSize % 16 == 0);
    static_assert(kViewConstantsByteSize % 16 == 0);
    static_assert(kInstanceConstantsByteSize % 16 == 0);
    static_assert(kMaterialParamsByteSize % 16 == 0);

    inline constexpr uint32_t kViewOffsetMatWorldToView = 0;
    inline constexpr uint32_t kViewOffsetMatViewToClip = 64;
    inline constexpr uint32_t kViewOffsetMatWorldToClip = 128;
    inline constexpr uint32_t kViewOffsetMatClipToView = 192;
    inline constexpr uint32_t kViewOffsetMatViewToWorld = 256;
    inline constexpr uint32_t kViewOffsetMatClipToWorld = 320;
    inline constexpr uint32_t kViewOffsetViewportOrigin = 384;
    inline constexpr uint32_t kViewOffsetViewportSize = 392;
    inline constexpr uint32_t kViewOffsetViewportSizeInv = 400;
    inline constexpr uint32_t kViewOffsetZNear = 408;
    inline constexpr uint32_t kViewOffsetFlags = 412;
    inline constexpr uint32_t kViewOffsetCameraPosition = 416;
    inline constexpr uint32_t kViewOffsetVerticalFovRadians = 432;
    inline constexpr uint32_t kViewOffsetAspectRatio = 436;

    inline constexpr uint32_t kMaterialOffsetBaseColorFactor = 0;
    inline constexpr uint32_t kMaterialOffsetRoughness = 12;
    inline constexpr uint32_t kMaterialOffsetMetallic = 16;
    inline constexpr uint32_t kMaterialOffsetOcclusionStrength = 20;
    inline constexpr uint32_t kMaterialOffsetNormalScale = 24;
    inline constexpr uint32_t kMaterialOffsetFlags = 28;

    // Opaque 1x1 fallback pixels for legally optional glTF textures. S1.4 uploads these
    // if a bound SRV is required. Missing optional textures never invent a second material model.
    inline constexpr donut::math::float4 kFallbackBaseColorRgba = {1.f, 1.f, 1.f, 1.f};
    inline constexpr donut::math::float4 kFallbackMetalRoughRgba = {1.f, 1.f, 1.f, 1.f}; // g=rough, b=metal
    inline constexpr donut::math::float4 kFallbackOcclusionRgba = {1.f, 0.f, 0.f, 1.f};  // r=occlusion
    inline constexpr donut::math::float4 kFallbackNormalRgba = {0.5f, 0.5f, 1.f, 1.f};

    enum class FallbackTextureKind : uint32_t
    {
        Scene = 0,
        WhiteOpaque,
        OcclusionWhite,
        FlatNormal
    };

    enum class DrawSkipReason : uint32_t
    {
        None = 0,
        NullMesh,
        NullMaterial,
        UnsupportedSpecularGloss,
        UnsupportedDomain,
        UnsupportedMeshType,
        UnsupportedPrimitive,
        UnsupportedSkinning,
        UnsupportedMetalnessPacking,
        MissingPosition,
        MissingIndices,
        MissingNormals
    };

    struct DiagnosticMessage
    {
        bool isError = false;
        DrawSkipReason reason = DrawSkipReason::None;
        std::string text;
    };

    struct ViewFillDesc
    {
        donut::math::affine3 worldToView = donut::math::affine3::identity();
        donut::math::float3 cameraPosition = 0.f;
        float verticalFovDegrees = 45.0f;
        float zNear = 0.1f;
        float viewportOriginX = 0.f;
        float viewportOriginY = 0.f;
        float viewportWidth = 1.f;
        float viewportHeight = 1.f;
    };

    struct VertexStream
    {
        bool present = false;
        nvrhi::Format format = nvrhi::Format::UNKNOWN;
        nvrhi::BufferRange range = {};
    };

    struct GeometryDrawDesc
    {
        nvrhi::IBuffer* vertexBuffer = nullptr;
        nvrhi::IBuffer* indexBuffer = nullptr;
        nvrhi::Format indexFormat = nvrhi::Format::R32_UINT;
        uint32_t indexOffset = 0;
        uint32_t vertexOffset = 0;
        uint32_t indexCount = 0;
        uint32_t vertexCount = 0;
        VertexStream position;
        VertexStream texCoord;
        VertexStream normal;
        VertexStream tangent;
    };

    struct TextureBinding
    {
        nvrhi::TextureHandle texture;
        FallbackTextureKind fallback = FallbackTextureKind::WhiteOpaque;
        donut::math::float4 fallbackRgba = kFallbackBaseColorRgba;
    };

    struct DrawRecord
    {
        uint32_t drawIndex = 0;
        uint32_t instanceIndex = 0;
        uint32_t meshIndex = 0;
        uint32_t geometryIndex = 0;
        int donutMaterialIndexInModel = -1;
        std::string meshName;
        std::string materialName;
        InstanceConstants instance;
        MaterialParams material;
        GeometryDrawDesc geometry;
        TextureBinding baseColor;
        TextureBinding metalRough;
        TextureBinding normal;
        TextureBinding occlusion;
        bool twoSided = false;
    };

    struct SceneDrawList
    {
        std::vector<DrawRecord> draws;
        std::vector<DiagnosticMessage> messages;
        uint32_t skippedCount = 0;
        uint32_t sourceInstanceCount = 0;
        uint32_t sourceMaterialCount = 0;
        uint32_t sourceMeshCount = 0;
    };

    struct CpuMaterialSample
    {
        donut::math::float3 baseColor = 1.f;
        float roughness = 1.f;
        float metallic = 1.f;
        float ao = 1.f;
        uint32_t materialFlags = 0;
        uint32_t gbufferFlags = kGBufferFlagShadingValid;
    };

    const char* DrawSkipReasonToString(DrawSkipReason reason);

    donut::math::affine3 MakeFirstPersonWorldToView(
        donut::math::float3 position,
        donut::math::float3 target,
        donut::math::float3 up);

    bool IsMirroredView(const donut::math::affine3& worldToView);

    FrameConstants MakeFrameConstants(uint32_t frameIndex);
    ViewConstants MakeViewConstants(const ViewFillDesc& desc);
    InstanceConstants MakeInstanceConstants(const donut::math::affine3& localToWorld);

    bool ConvertMaterial(
        const donut::engine::Material& material,
        MaterialParams& params,
        TextureBinding& baseColor,
        TextureBinding& metalRough,
        TextureBinding& normal,
        TextureBinding& occlusion,
        std::vector<DiagnosticMessage>* diagnostics);

    CpuMaterialSample SampleMaterialWithoutTextures(const MaterialParams& params);

    SceneDrawList BuildSceneDrawList(const donut::engine::SceneGraph& graph);

    bool ValidateRendererDataLayout(std::string& error);
}
