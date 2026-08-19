#include "RendererData.h"

#include <donut/engine/SceneGraph.h>

#include <string>

using namespace donut;
using namespace donut::math;

namespace renderlab
{
    namespace
    {
        void AddDiagnostic(
            std::vector<DiagnosticMessage>* diagnostics,
            bool isError,
            DrawSkipReason reason,
            const std::string& text)
        {
            if (!diagnostics)
            {
                return;
            }

            DiagnosticMessage message;
            message.isError = isError;
            message.reason = reason;
            message.text = text;
            diagnostics->push_back(std::move(message));
        }

        std::string FormatSkip(const char* name, DrawSkipReason reason, const char* detail)
        {
            std::string text = "Unsupported scene data";
            if (name && name[0] != '\0')
            {
                text += " '";
                text += name;
                text += "'";
            }
            text += ": ";
            text += DrawSkipReasonToString(reason);
            if (detail && detail[0] != '\0')
            {
                text += " (";
                text += detail;
                text += ")";
            }
            return text;
        }

        TextureBinding MakeTextureBinding(
            const std::shared_ptr<engine::LoadedTexture>& texture,
            bool enabled,
            FallbackTextureKind missingKind,
            const float4& missingRgba)
        {
            TextureBinding binding;
            const bool hasSlot = enabled && texture != nullptr;
            if (hasSlot)
            {
                binding.texture = texture->texture;
                binding.fallback = FallbackTextureKind::Scene;
                binding.fallbackRgba = missingRgba;
            }
            else
            {
                binding.texture = nullptr;
                binding.fallback = missingKind;
                binding.fallbackRgba = missingRgba;
            }
            return binding;
        }

        VertexStream MakeVertexStream(
            const engine::BufferGroup* buffers,
            engine::VertexAttribute attribute,
            nvrhi::Format format)
        {
            VertexStream stream;
            if (!buffers || !buffers->hasAttribute(attribute))
            {
                return stream;
            }
            stream.present = true;
            stream.format = format;
            stream.range = buffers->getVertexBufferRange(attribute);
            return stream;
        }

        uint32_t FieldOffset(const void* base, const void* field)
        {
            return static_cast<uint32_t>(
                static_cast<const char*>(field) - static_cast<const char*>(base));
        }

        bool CheckOffset(
            std::string& error,
            const char* name,
            uint32_t actual,
            uint32_t expected)
        {
            if (actual == expected)
            {
                return true;
            }
            error = std::string(name) + " offset is " + std::to_string(actual) +
                    ", expected " + std::to_string(expected);
            return false;
        }

        bool CheckSize(
            std::string& error,
            const char* name,
            size_t actual,
            uint32_t expected)
        {
            if (actual == expected)
            {
                return true;
            }
            error = std::string(name) + " size is " + std::to_string(actual) +
                    ", expected " + std::to_string(expected);
            return false;
        }
    }

    const char* DrawSkipReasonToString(DrawSkipReason reason)
    {
        switch (reason)
        {
        case DrawSkipReason::None:
            return "none";
        case DrawSkipReason::NullMesh:
            return "mesh instance has no mesh";
        case DrawSkipReason::NullMaterial:
            return "geometry has no material";
        case DrawSkipReason::UnsupportedSpecularGloss:
            return "specular-glossiness is not the metal-rough GBuffer contract";
        case DrawSkipReason::UnsupportedDomain:
            return "material domain is not opaque";
        case DrawSkipReason::UnsupportedMeshType:
            return "mesh type is not triangle geometry";
        case DrawSkipReason::UnsupportedPrimitive:
            return "primitive type is not triangles";
        case DrawSkipReason::UnsupportedSkinning:
            return "skinned meshes are not mapped in Stage 1";
        case DrawSkipReason::UnsupportedMetalnessPacking:
            return "metalness-in-red packing is not ORM.g/b";
        case DrawSkipReason::MissingPosition:
            return "POSITION is required and missing";
        case DrawSkipReason::MissingIndices:
            return "indexed triangles are required and the index buffer is missing";
        case DrawSkipReason::MissingNormals:
            return "NORMAL is required for the GBuffer shading normal and is missing";
        default:
            return "unknown";
        }
    }

    affine3 MakeFirstPersonWorldToView(float3 position, float3 target, float3 up)
    {
        // Matches donut::app::BaseCamera::BaseLookAt + UpdateWorldToView.
        const float3 direction = normalize(target - position);
        const float3 right = normalize(cross(direction, up));
        const float3 cameraUp = normalize(cross(right, direction));
        const affine3 translated = affine3::from_cols(right, cameraUp, direction, float3(0.f));
        return translation(-position) * translated;
    }

    bool IsMirroredView(const affine3& worldToView)
    {
        return dm::determinant(worldToView.m_linear) < 0.f;
    }

    FrameConstants MakeFrameConstants(uint32_t frameIndex)
    {
        FrameConstants constants = {};
        constants.frameIndex = frameIndex;
        return constants;
    }

    ViewConstants MakeViewConstants(const ViewFillDesc& desc)
    {
        ViewConstants constants = {};

        const float width = desc.viewportWidth > 0.f ? desc.viewportWidth : 1.f;
        const float height = desc.viewportHeight > 0.f ? desc.viewportHeight : 1.f;
        const float aspect = width / height;
        const float fovRadians = dm::radians(desc.verticalFovDegrees);
        const float4x4 worldToView = affineToHomogeneous(desc.worldToView);
        const float4x4 viewToClip = perspProjD3DStyleReverse(fovRadians, aspect, desc.zNear);
        const float4x4 worldToClip = worldToView * viewToClip;
        const float4x4 viewToWorld = affineToHomogeneous(inverse(desc.worldToView));
        const float4x4 clipToView = inverse(viewToClip);
        const float4x4 clipToWorld = inverse(worldToClip);

        constants.matWorldToView = worldToView;
        constants.matViewToClip = viewToClip;
        constants.matWorldToClip = worldToClip;
        constants.matClipToView = clipToView;
        constants.matViewToWorld = viewToWorld;
        constants.matClipToWorld = clipToWorld;
        constants.viewportOrigin = dm::float2(desc.viewportOriginX, desc.viewportOriginY);
        constants.viewportSize = dm::float2(width, height);
        constants.viewportSizeInv = 1.f / constants.viewportSize;
        constants.zNear = desc.zNear;
        constants.flags = IsMirroredView(desc.worldToView) ? RendererViewFlag_Mirrored : 0u;
        constants.cameraPosition = float4(desc.cameraPosition, 1.f);
        constants.verticalFovRadians = fovRadians;
        constants.aspectRatio = aspect;
        return constants;
    }

    InstanceConstants MakeInstanceConstants(const affine3& localToWorld)
    {
        InstanceConstants constants = {};
        constants.matLocalToWorld = affineToHomogeneous(localToWorld);
        constants.matWorldToLocal = affineToHomogeneous(inverse(localToWorld));
        return constants;
    }

    bool ConvertMaterial(
        const engine::Material& material,
        MaterialParams& params,
        TextureBinding& baseColor,
        TextureBinding& metalRough,
        TextureBinding& normal,
        TextureBinding& occlusion,
        std::vector<DiagnosticMessage>* diagnostics)
    {
        params = {};
        const char* name = material.name.empty() ? "<unnamed>" : material.name.c_str();

        if (material.useSpecularGlossModel)
        {
            AddDiagnostic(
                diagnostics,
                true,
                DrawSkipReason::UnsupportedSpecularGloss,
                FormatSkip(name, DrawSkipReason::UnsupportedSpecularGloss, nullptr));
            return false;
        }

        if (material.domain != engine::MaterialDomain::Opaque)
        {
            AddDiagnostic(
                diagnostics,
                true,
                DrawSkipReason::UnsupportedDomain,
                FormatSkip(
                    name,
                    DrawSkipReason::UnsupportedDomain,
                    engine::MaterialDomainToString(material.domain)));
            return false;
        }

        if (material.metalnessInRedChannel)
        {
            AddDiagnostic(
                diagnostics,
                true,
                DrawSkipReason::UnsupportedMetalnessPacking,
                FormatSkip(name, DrawSkipReason::UnsupportedMetalnessPacking, nullptr));
            return false;
        }

        if (material.enableHair || material.enableSubsurfaceScattering)
        {
            AddDiagnostic(
                diagnostics,
                true,
                DrawSkipReason::UnsupportedDomain,
                FormatSkip(name, DrawSkipReason::UnsupportedDomain, "hair or SSS shading model"));
            return false;
        }

        params.baseColorFactor = material.baseOrDiffuseColor;
        params.roughness = material.roughness;
        params.metallic = material.metalness;
        params.occlusionStrength = material.occlusionStrength;
        params.normalScale = material.normalTextureScale;
        params.flags = 0;

        baseColor = MakeTextureBinding(
            material.baseOrDiffuseTexture,
            material.enableBaseOrDiffuseTexture,
            FallbackTextureKind::WhiteOpaque,
            kFallbackBaseColorRgba);
        metalRough = MakeTextureBinding(
            material.metalRoughOrSpecularTexture,
            material.enableMetalRoughOrSpecularTexture,
            FallbackTextureKind::WhiteOpaque,
            kFallbackMetalRoughRgba);
        normal = MakeTextureBinding(
            material.normalTexture,
            material.enableNormalTexture,
            FallbackTextureKind::FlatNormal,
            kFallbackNormalRgba);
        occlusion = MakeTextureBinding(
            material.occlusionTexture,
            material.enableOcclusionTexture,
            FallbackTextureKind::OcclusionWhite,
            kFallbackOcclusionRgba);

        if (baseColor.fallback == FallbackTextureKind::Scene)
        {
            params.flags |= MaterialFlag_HasBaseColorTexture;
        }
        if (metalRough.fallback == FallbackTextureKind::Scene)
        {
            params.flags |= MaterialFlag_HasMetalRoughTexture;
        }
        if (normal.fallback == FallbackTextureKind::Scene)
        {
            params.flags |= MaterialFlag_HasNormalTexture;
        }
        if (occlusion.fallback == FallbackTextureKind::Scene)
        {
            params.flags |= MaterialFlag_HasOcclusionTexture;
        }
        else
        {
            params.occlusionStrength = 0.f;
        }
        if (material.doubleSided)
        {
            params.flags |= MaterialFlag_TwoSided;
        }

        return true;
    }

    CpuMaterialSample SampleMaterialWithoutTextures(const MaterialParams& params)
    {
        CpuMaterialSample sample;
        sample.baseColor = params.baseColorFactor;
        sample.roughness = params.roughness;
        sample.metallic = params.metallic;
        sample.ao = 1.f;
        if ((params.flags & MaterialFlag_HasOcclusionTexture) != 0)
        {
            sample.ao = dm::lerp(1.f, 1.f, params.occlusionStrength);
        }
        sample.materialFlags = params.flags;
        sample.gbufferFlags = kGBufferFlagShadingValid;
        if ((params.flags & MaterialFlag_TwoSided) != 0)
        {
            sample.gbufferFlags |= kGBufferFlagTwoSided;
        }
        return sample;
    }

    SceneDrawList BuildSceneDrawList(const engine::SceneGraph& graph)
    {
        SceneDrawList list;
        list.sourceMeshCount = static_cast<uint32_t>(graph.GetMeshes().size());
        list.sourceMaterialCount = static_cast<uint32_t>(graph.GetMaterials().size());
        list.sourceInstanceCount = static_cast<uint32_t>(graph.GetMeshInstances().size());

        uint32_t sourceInstanceIndex = 0;
        for (const std::shared_ptr<engine::MeshInstance>& instance : graph.GetMeshInstances())
        {
            const uint32_t instanceIndex = sourceInstanceIndex++;
            if (!instance)
            {
                continue;
            }

            const char* instanceName = instance->GetName().c_str();
            if (dynamic_cast<const engine::SkinnedMeshInstance*>(instance.get()))
            {
                ++list.skippedCount;
                AddDiagnostic(
                    &list.messages,
                    true,
                    DrawSkipReason::UnsupportedSkinning,
                    FormatSkip(instanceName, DrawSkipReason::UnsupportedSkinning, nullptr));
                continue;
            }

            const std::shared_ptr<engine::MeshInfo>& mesh = instance->GetMesh();
            if (!mesh)
            {
                ++list.skippedCount;
                AddDiagnostic(
                    &list.messages,
                    true,
                    DrawSkipReason::NullMesh,
                    FormatSkip(instanceName, DrawSkipReason::NullMesh, nullptr));
                continue;
            }

            if (mesh->type != engine::MeshType::Triangles)
            {
                ++list.skippedCount;
                AddDiagnostic(
                    &list.messages,
                    true,
                    DrawSkipReason::UnsupportedMeshType,
                    FormatSkip(mesh->name.c_str(), DrawSkipReason::UnsupportedMeshType, nullptr));
                continue;
            }

            affine3 localToWorld = affine3::identity();
            if (const engine::SceneGraphNode* node = instance->GetNode())
            {
                localToWorld = node->GetLocalToWorldTransformFloat();
            }
            const InstanceConstants instanceConstants = MakeInstanceConstants(localToWorld);

            uint32_t geometryIndex = 0;
            for (const std::shared_ptr<engine::MeshGeometry>& geometry : mesh->geometries)
            {
                const uint32_t thisGeometryIndex = geometryIndex++;
                if (!geometry)
                {
                    continue;
                }
                if (!geometry->material)
                {
                    ++list.skippedCount;
                    AddDiagnostic(
                        &list.messages,
                        true,
                        DrawSkipReason::NullMaterial,
                        FormatSkip(mesh->name.c_str(), DrawSkipReason::NullMaterial, nullptr));
                    continue;
                }
                if (geometry->type != engine::MeshGeometryPrimitiveType::Triangles)
                {
                    ++list.skippedCount;
                    AddDiagnostic(
                        &list.messages,
                        true,
                        DrawSkipReason::UnsupportedPrimitive,
                        FormatSkip(mesh->name.c_str(), DrawSkipReason::UnsupportedPrimitive, nullptr));
                    continue;
                }

                DrawRecord record;
                record.instanceIndex = instanceIndex;
                record.meshIndex = static_cast<uint32_t>(mesh->globalMeshIndex);
                record.geometryIndex = thisGeometryIndex;
                record.donutMaterialIndexInModel = geometry->material->materialIndexInModel;
                record.meshName = mesh->name;
                record.materialName = geometry->material->name;
                record.instance = instanceConstants;
                record.twoSided = geometry->material->doubleSided;

                if (!ConvertMaterial(
                        *geometry->material,
                        record.material,
                        record.baseColor,
                        record.metalRough,
                        record.normal,
                        record.occlusion,
                        &list.messages))
                {
                    ++list.skippedCount;
                    continue;
                }

                if (mesh->buffers)
                {
                    const engine::BufferGroup& buffers = *mesh->buffers;
                    if (!buffers.hasAttribute(engine::VertexAttribute::Position))
                    {
                        ++list.skippedCount;
                        AddDiagnostic(
                            &list.messages,
                            true,
                            DrawSkipReason::MissingPosition,
                            FormatSkip(mesh->name.c_str(), DrawSkipReason::MissingPosition, nullptr));
                        continue;
                    }
                    if (!buffers.indexBuffer && buffers.indexData.empty())
                    {
                        ++list.skippedCount;
                        AddDiagnostic(
                            &list.messages,
                            true,
                            DrawSkipReason::MissingIndices,
                            FormatSkip(mesh->name.c_str(), DrawSkipReason::MissingIndices, nullptr));
                        continue;
                    }
                    if (!buffers.hasAttribute(engine::VertexAttribute::Normal))
                    {
                        ++list.skippedCount;
                        AddDiagnostic(
                            &list.messages,
                            true,
                            DrawSkipReason::MissingNormals,
                            FormatSkip(mesh->name.c_str(), DrawSkipReason::MissingNormals, nullptr));
                        continue;
                    }

                    record.geometry.vertexBuffer = buffers.vertexBuffer;
                    record.geometry.indexBuffer = buffers.indexBuffer;
                    record.geometry.position = MakeVertexStream(
                        &buffers, engine::VertexAttribute::Position, nvrhi::Format::RGB32_FLOAT);
                    record.geometry.texCoord = MakeVertexStream(
                        &buffers, engine::VertexAttribute::TexCoord1, nvrhi::Format::RG32_FLOAT);
                    record.geometry.normal = MakeVertexStream(
                        &buffers, engine::VertexAttribute::Normal, nvrhi::Format::RGBA8_SNORM);
                    record.geometry.tangent = MakeVertexStream(
                        &buffers, engine::VertexAttribute::Tangent, nvrhi::Format::RGBA8_SNORM);
                }

                record.geometry.indexFormat = nvrhi::Format::R32_UINT;
                record.geometry.indexOffset = mesh->indexOffset + geometry->indexOffsetInMesh;
                record.geometry.vertexOffset = mesh->vertexOffset + geometry->vertexOffsetInMesh;
                record.geometry.indexCount = geometry->numIndices;
                record.geometry.vertexCount = geometry->numVertices;
                record.drawIndex = static_cast<uint32_t>(list.draws.size());
                list.draws.push_back(std::move(record));
            }
        }

        return list;
    }

    bool ValidateRendererDataLayout(std::string& error)
    {
        if (!CheckSize(error, "FrameConstants", sizeof(FrameConstants), kFrameConstantsByteSize) ||
            !CheckSize(error, "ViewConstants", sizeof(ViewConstants), kViewConstantsByteSize) ||
            !CheckSize(error, "InstanceConstants", sizeof(InstanceConstants), kInstanceConstantsByteSize) ||
            !CheckSize(error, "MaterialParams", sizeof(MaterialParams), kMaterialParamsByteSize))
        {
            return false;
        }

        ViewConstants view = {};
        if (!CheckOffset(error, "ViewConstants.matWorldToView", FieldOffset(&view, &view.matWorldToView), kViewOffsetMatWorldToView) ||
            !CheckOffset(error, "ViewConstants.matViewToClip", FieldOffset(&view, &view.matViewToClip), kViewOffsetMatViewToClip) ||
            !CheckOffset(error, "ViewConstants.matWorldToClip", FieldOffset(&view, &view.matWorldToClip), kViewOffsetMatWorldToClip) ||
            !CheckOffset(error, "ViewConstants.matClipToView", FieldOffset(&view, &view.matClipToView), kViewOffsetMatClipToView) ||
            !CheckOffset(error, "ViewConstants.matViewToWorld", FieldOffset(&view, &view.matViewToWorld), kViewOffsetMatViewToWorld) ||
            !CheckOffset(error, "ViewConstants.matClipToWorld", FieldOffset(&view, &view.matClipToWorld), kViewOffsetMatClipToWorld) ||
            !CheckOffset(error, "ViewConstants.viewportOrigin", FieldOffset(&view, &view.viewportOrigin), kViewOffsetViewportOrigin) ||
            !CheckOffset(error, "ViewConstants.viewportSize", FieldOffset(&view, &view.viewportSize), kViewOffsetViewportSize) ||
            !CheckOffset(error, "ViewConstants.viewportSizeInv", FieldOffset(&view, &view.viewportSizeInv), kViewOffsetViewportSizeInv) ||
            !CheckOffset(error, "ViewConstants.zNear", FieldOffset(&view, &view.zNear), kViewOffsetZNear) ||
            !CheckOffset(error, "ViewConstants.flags", FieldOffset(&view, &view.flags), kViewOffsetFlags) ||
            !CheckOffset(error, "ViewConstants.cameraPosition", FieldOffset(&view, &view.cameraPosition), kViewOffsetCameraPosition) ||
            !CheckOffset(error, "ViewConstants.verticalFovRadians", FieldOffset(&view, &view.verticalFovRadians), kViewOffsetVerticalFovRadians) ||
            !CheckOffset(error, "ViewConstants.aspectRatio", FieldOffset(&view, &view.aspectRatio), kViewOffsetAspectRatio))
        {
            return false;
        }

        InstanceConstants instance = {};
        if (!CheckOffset(error, "InstanceConstants.matLocalToWorld", FieldOffset(&instance, &instance.matLocalToWorld), 0) ||
            !CheckOffset(error, "InstanceConstants.matWorldToLocal", FieldOffset(&instance, &instance.matWorldToLocal), 64))
        {
            return false;
        }

        MaterialParams material = {};
        if (!CheckOffset(error, "MaterialParams.baseColorFactor", FieldOffset(&material, &material.baseColorFactor), kMaterialOffsetBaseColorFactor) ||
            !CheckOffset(error, "MaterialParams.roughness", FieldOffset(&material, &material.roughness), kMaterialOffsetRoughness) ||
            !CheckOffset(error, "MaterialParams.metallic", FieldOffset(&material, &material.metallic), kMaterialOffsetMetallic) ||
            !CheckOffset(error, "MaterialParams.occlusionStrength", FieldOffset(&material, &material.occlusionStrength), kMaterialOffsetOcclusionStrength) ||
            !CheckOffset(error, "MaterialParams.normalScale", FieldOffset(&material, &material.normalScale), kMaterialOffsetNormalScale) ||
            !CheckOffset(error, "MaterialParams.flags", FieldOffset(&material, &material.flags), kMaterialOffsetFlags))
        {
            return false;
        }

        return true;
    }
}
