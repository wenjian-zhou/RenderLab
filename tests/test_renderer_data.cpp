#include "renderer/RendererData.h"
#include "renderer/GBufferContract.h"

#include <donut/core/log.h>
#include <donut/engine/SceneGraph.h>
#include <donut/engine/SceneTypes.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#if defined(_MSC_VER)
#include <crtdbg.h>
#endif

using namespace donut;
using namespace donut::math;
using namespace renderlab;

int RunGBufferTargetContractTests();
int RunGBufferPassContractTests();
int RunGBufferDebugPassTests();
int RunImageCompareTests();
int RunLightingContractTests();
int RunLightingDebugPassTests();
int RunHdrCompareTests();
int RunPostProcessContractTests();

namespace
{
    int g_failures = 0;

    void Check(bool condition, const char* message)
    {
        if (condition)
        {
            std::printf("  PASS  %s\n", message);
            return;
        }
        std::printf("  FAIL  %s\n", message);
        ++g_failures;
    }

    bool Near(float actual, float expected, float tol)
    {
        return std::fabs(actual - expected) <= tol;
    }

    bool Near(const float3& actual, const float3& expected, float tol)
    {
        return Near(actual.x, expected.x, tol) &&
               Near(actual.y, expected.y, tol) &&
               Near(actual.z, expected.z, tol);
    }

    bool MatricesDiffer(const float4x4& a, const float4x4& b, float tol)
    {
        for (int i = 0; i < 16; ++i)
        {
            if (std::fabs(a.m_data[i] - b.m_data[i]) > tol)
            {
                return true;
            }
        }
        return false;
    }

    float3 TranslationOf(const float4x4& matrix)
    {
        return float3(matrix[3][0], matrix[3][1], matrix[3][2]);
    }

    std::shared_ptr<engine::Material> MakeOpaqueMetalRough(
        const char* name,
        float3 baseColor,
        float metallic,
        float roughness)
    {
        auto material = std::make_shared<engine::Material>();
        material->name = name;
        material->domain = engine::MaterialDomain::Opaque;
        material->useSpecularGlossModel = false;
        material->baseOrDiffuseColor = baseColor;
        material->metalness = metallic;
        material->roughness = roughness;
        material->occlusionStrength = 1.f;
        material->normalTextureScale = 1.f;
        return material;
    }

    std::shared_ptr<engine::MeshInfo> MakeTriangleMesh(
        const char* name,
        const std::shared_ptr<engine::Material>& material)
    {
        auto mesh = std::make_shared<engine::MeshInfo>();
        mesh->name = name;
        mesh->type = engine::MeshType::Triangles;
        auto geometry = std::make_shared<engine::MeshGeometry>();
        geometry->material = material;
        geometry->type = engine::MeshGeometryPrimitiveType::Triangles;
        geometry->numIndices = 36;
        geometry->numVertices = 24;
        mesh->geometries.push_back(std::move(geometry));
        return mesh;
    }
}

int main()
{
#if defined(_MSC_VER)
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    _set_error_mode(_OUT_TO_STDERR);
    _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_WARN, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif

    log::ConsoleApplicationMode();
    log::SetMinSeverity(log::Severity::None);

    std::printf("RenderLab S1.2 data-contract tests\n");

    std::string layoutError;
    Check(ValidateRendererDataLayout(layoutError), "C++ struct sizes and offsets match the HLSL contract");
    if (!layoutError.empty() && g_failures > 0)
    {
        std::printf("    %s\n", layoutError.c_str());
    }

    const uint32_t packedFlags = kGBufferFlagShadingValid | kGBufferFlagTwoSided;
    Check(
        UnpackGBufferFlags(PackGBufferFlags(packedFlags)) == packedFlags,
        "GBuffer flag pack/unpack round-trips 8-bit UNORM bits");

    const float3 dielectricColor(0.18f, 0.32f, 0.72f);
    const float3 metalColor(1.0f, 0.766f, 0.336f);
    auto dielectric = MakeOpaqueMetalRough("BlueDielectric", dielectricColor, 0.f, 0.75f);
    auto metal = MakeOpaqueMetalRough("GoldMetal", metalColor, 1.f, 0.2f);

    MaterialParams dielectricParams = {};
    MaterialParams metalParams = {};
    TextureBinding unused[8];
    std::vector<DiagnosticMessage> messages;
    Check(
        ConvertMaterial(*dielectric, dielectricParams, unused[0], unused[1], unused[2], unused[3], &messages),
        "BlueDielectric converts");
    Check(
        ConvertMaterial(*metal, metalParams, unused[4], unused[5], unused[6], unused[7], &messages),
        "GoldMetal converts");
    Check(messages.empty(), "Opaque metal-rough conversion emits no diagnostics");
    Check(
        MatricesDiffer(
            float4x4::identity(),
            float4x4::identity(),
            0.f) == false,
        "Identity matrices compare equal");
    Check(
        dielectricParams.baseColorFactor.x != metalParams.baseColorFactor.x ||
            dielectricParams.metallic != metalParams.metallic ||
            dielectricParams.roughness != metalParams.roughness,
        "Two materials produce distinct MaterialParams");
    Check(Near(dielectricParams.baseColorFactor, dielectricColor, 1e-6f), "Dielectric baseColor is linear factor");
    Check(Near(dielectricParams.roughness, 0.75f, 1e-6f), "Dielectric roughness is perceptual 0.75");
    Check(Near(dielectricParams.metallic, 0.f, 1e-6f), "Dielectric metallic is 0");
    Check(Near(metalParams.baseColorFactor, metalColor, 1e-6f), "Metal baseColor is linear factor");
    Check(Near(metalParams.roughness, 0.2f, 1e-6f), "Metal roughness is perceptual 0.2");
    Check(Near(metalParams.metallic, 1.f, 1e-6f), "Metal metallic is 1");
    Check((dielectricParams.flags & MaterialFlag_HasBaseColorTexture) == 0, "Missing baseColorTexture uses factor only");
    Check(dielectricParams.occlusionStrength == 0.f, "Missing occlusionTexture forces strength 0 so AO = 1");
    Check(unused[0].fallback == FallbackTextureKind::WhiteOpaque, "Optional baseColor uses white fallback");
    Check(unused[2].fallback == FallbackTextureKind::FlatNormal, "Optional normal uses flat-normal fallback");
    Check(unused[3].fallback == FallbackTextureKind::OcclusionWhite, "Optional occlusion uses white.r fallback");

    const CpuMaterialSample dielectricSample = SampleMaterialWithoutTextures(dielectricParams);
    Check(Near(dielectricSample.ao, 1.f, 1e-6f), "Missing occlusion evaluates AO = 1");
    Check((dielectricSample.gbufferFlags & kGBufferFlagShadingValid) != 0, "CPU sample sets ShadingValid");

    MaterialParams again = {};
    TextureBinding againBindings[4];
    Check(ConvertMaterial(*dielectric, again, againBindings[0], againBindings[1], againBindings[2], againBindings[3], nullptr),
          "Conversion is repeatable");
    Check(std::memcmp(&dielectricParams, &again, sizeof(MaterialParams)) == 0, "Material conversion is deterministic");

    auto specGloss = MakeOpaqueMetalRough("SpecGloss", dielectricColor, 0.f, 0.5f);
    specGloss->useSpecularGlossModel = true;
    MaterialParams ignored = {};
    TextureBinding specBindings[4];
    std::vector<DiagnosticMessage> specMessages;
    Check(
        !ConvertMaterial(*specGloss, ignored, specBindings[0], specBindings[1], specBindings[2], specBindings[3], &specMessages),
        "Specular-gloss is rejected");
    Check(!specMessages.empty() && specMessages.front().isError, "Specular-gloss logs required unsupported data");

    auto alpha = MakeOpaqueMetalRough("AlphaCutout", dielectricColor, 0.f, 0.5f);
    alpha->domain = engine::MaterialDomain::AlphaTested;
    std::vector<DiagnosticMessage> alphaMessages;
    Check(
        !ConvertMaterial(*alpha, ignored, specBindings[0], specBindings[1], specBindings[2], specBindings[3], &alphaMessages),
        "Alpha-tested domain is rejected in Stage 1");

    auto graph = std::make_shared<engine::SceneGraph>();
    auto root = std::make_shared<engine::SceneGraphNode>();
    graph->SetRootNode(root);
    auto meshA = MakeTriangleMesh("DielectricBox", dielectric);
    auto meshB = MakeTriangleMesh("MetalBox", metal);
    auto instanceA = std::make_shared<engine::MeshInstance>(meshA);
    auto instanceB = std::make_shared<engine::MeshInstance>(meshB);
    auto nodeA = graph->AttachLeafNode(root, instanceA);
    auto nodeB = graph->AttachLeafNode(root, instanceB);
    nodeA->SetName("DielectricBox");
    nodeB->SetName("MetalBox");
    nodeA->SetTranslation(double3(-1.2, 0.25, 0.0));
    nodeB->SetTranslation(double3(1.2, 0.25, 0.0));
    graph->Refresh(0);

    const SceneDrawList drawList = BuildSceneDrawList(*graph);
    Check(drawList.draws.size() == 2, "Two mesh instances produce two draw records");
    Check(drawList.skippedCount == 0, "Supported opaque meshes are not skipped");
    Check(
        MatricesDiffer(drawList.draws[0].instance.matLocalToWorld, drawList.draws[1].instance.matLocalToWorld, 1e-6f),
        "Two transforms produce distinct instance matrices");
    Check(
        Near(TranslationOf(drawList.draws[0].instance.matLocalToWorld), float3(-1.2f, 0.25f, 0.f), 1e-5f),
        "First instance translation is (-1.2, 0.25, 0)");
    Check(
        Near(TranslationOf(drawList.draws[1].instance.matLocalToWorld), float3(1.2f, 0.25f, 0.f), 1e-5f),
        "Second instance translation is (1.2, 0.25, 0)");
    Check(drawList.draws[0].material.metallic != drawList.draws[1].material.metallic, "Draw records keep distinct materials");
    Check(drawList.draws[0].geometry.indexCount == 36, "Geometry index count is mapped");
    Check(drawList.draws[0].geometry.vertexCount == 24, "Geometry vertex count is mapped");

    const float3 cameraPos(4.8f, 2.4f, 5.6f);
    const float3 cameraTarget(0.f, 0.85f, 0.f);
    const float3 cameraUp(0.f, 1.f, 0.f);
    ViewFillDesc viewDesc;
    viewDesc.worldToView = MakeFirstPersonWorldToView(cameraPos, cameraTarget, cameraUp);
    viewDesc.cameraPosition = cameraPos;
    viewDesc.verticalFovDegrees = 45.f;
    viewDesc.zNear = 0.1f;
    viewDesc.viewportWidth = 1280.f;
    viewDesc.viewportHeight = 720.f;
    const ViewConstants view = MakeViewConstants(viewDesc);
    Check(IsMirroredView(viewDesc.worldToView), "FirstPersonCamera world-to-view linear determinant is negative");
    Check((view.flags & RendererViewFlag_Mirrored) != 0, "View flags record mirrored / frontCounterClockwise");
    Check(Near(view.zNear, 0.1f, 1e-6f), "zNear is 0.1");
    Check(Near(view.verticalFovRadians, radians(45.f), 1e-6f), "FOV 45 deg is converted to radians");
    const float expectedYScale = 1.f / std::tan(0.5f * radians(45.f));
    const float degreesMistakenAsRadians = 1.f / std::tan(0.5f * 45.f);
    Check(Near(view.matViewToClip[1][1], expectedYScale, 1e-5f), "Projection uses FOV in radians");
    Check(!Near(view.matViewToClip[1][1], degreesMistakenAsRadians, 0.05f), "Projection is not 45-as-radians");
    Check(Near(view.viewportSize.x, 1280.f, 1e-6f) && Near(view.viewportSize.y, 720.f, 1e-6f), "Viewport size is copied");

    const float4 worldPos(1.f, 2.f, 3.f, 1.f);
    const float4 clipPos = worldPos * view.matWorldToClip;
    float4 recovered = clipPos * view.matClipToWorld;
    recovered /= recovered.w;
    Check(Near(float3(recovered.x, recovered.y, recovered.z), float3(1.f, 2.f, 3.f), 1e-4f),
          "World -> clip -> world round-trips with row-vector mul");

    const FrameConstants frame = MakeFrameConstants(12);
    Check(frame.frameIndex == 12, "FrameConstants stores the frame index");

    g_failures += RunLightingContractTests();
    g_failures += RunLightingDebugPassTests();
    g_failures += RunGBufferTargetContractTests();
    g_failures += RunGBufferPassContractTests();
    g_failures += RunGBufferDebugPassTests();
    g_failures += RunImageCompareTests();
    g_failures += RunHdrCompareTests();
    g_failures += RunPostProcessContractTests();

    std::printf("\n%d failure(s)\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
