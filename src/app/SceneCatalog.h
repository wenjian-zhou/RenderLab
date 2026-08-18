#pragma once

#include <donut/core/math/math.h>
#include <donut/core/vfs/VFS.h>

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace renderlab
{
    struct CameraPreset
    {
        std::string name = "s04-default";
        donut::math::float3 position = {4.800f, 2.400f, 5.600f};
        donut::math::float3 target = {0.000f, 0.850f, 0.000f};
        donut::math::float3 up = {0.000f, 1.000f, 0.000f};
        float verticalFovDegrees = 45.0f;
        float zNear = 0.1f;
    };

    struct SceneFile
    {
        std::string relativePath;
        std::string sha256;
        uint64_t sizeBytes = 0;
    };

    struct SceneAsset
    {
        std::string id;
        std::string role;
        std::string displayName;
        std::string relativePath;
        std::string url;
        std::string license;
        std::string sha256;
        std::vector<SceneFile> files;
    };

    struct ResolvedScene
    {
        std::string id;
        std::string displayName;
        std::string relativePath;
        std::string virtualPath;
        std::string expectedSha256;
        std::string url;
        std::string license;
        std::vector<SceneFile> files;
        bool integrityRequired = false;
    };

    class SceneCatalog
    {
    public:
        static bool Load(
            donut::vfs::IFileSystem& fs,
            const std::filesystem::path& manifestVirtualPath,
            SceneCatalog& catalog,
            std::string& error);

        const CameraPreset& GetCameraPreset() const { return m_camera; }
        const std::string& GetDefaultSceneId() const { return m_defaultSceneId; }
        const std::vector<SceneAsset>& GetScenes() const { return m_scenes; }

        const SceneAsset* FindById(std::string_view id) const;
        const SceneAsset* FindByRelativePath(std::string_view relativePath) const;
        const SceneAsset* GetDefaultScene() const;
        const SceneAsset* GetFallbackScene() const;

        bool Resolve(
            const std::optional<std::string>& sceneArgument,
            ResolvedScene& resolved,
            std::string& error) const;

    private:
        CameraPreset m_camera;
        std::string m_defaultSceneId = "cesium-milk-truck";
        std::vector<SceneAsset> m_scenes;
    };

    std::string ComputeSha256Hex(const void* data, size_t size);
    bool VerifySceneIntegrity(
        donut::vfs::IFileSystem& fs,
        const ResolvedScene& scene,
        std::string& error);
}
