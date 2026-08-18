#include "SceneCatalog.h"

#include <donut/core/json.h>
#include <donut/core/log.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <sstream>

#pragma comment(lib, "bcrypt.lib")

using namespace donut;

namespace renderlab
{
    namespace
    {
        std::string ToLowerAscii(std::string value)
        {
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
                return static_cast<char>(std::tolower(ch));
            });
            return value;
        }

        std::string NormalizeRelativePath(std::string_view raw)
        {
            std::filesystem::path path(raw);
            std::string generic = path.lexically_normal().generic_string();
            while (generic.starts_with("./"))
            {
                generic.erase(0, 2);
            }
            if (generic.starts_with("scenes/"))
            {
                generic.erase(0, 7);
            }
            else if (generic.starts_with("/scenes/"))
            {
                generic.erase(0, 8);
            }
            while (generic.starts_with("/"))
            {
                generic.erase(0, 1);
            }
            return generic;
        }

        bool LooksAbsolute(const std::filesystem::path& path)
        {
            // Reject drive-qualified or UNC paths so Donut never logs a machine path.
            // Virtual paths such as /scenes/... are allowed.
            return path.has_root_name();
        }

        dm::float3 ReadFloat3(const Json::Value& node, const dm::float3& fallback)
        {
            if (!node.isArray() || node.size() < 3)
            {
                return fallback;
            }
            return dm::float3(
                node[0].asFloat(),
                node[1].asFloat(),
                node[2].asFloat());
        }

        SceneAsset ReadSceneAsset(const Json::Value& node)
        {
            SceneAsset asset;
            asset.id = json::Read<std::string>(node["id"], "");
            asset.role = json::Read<std::string>(node["role"], "");
            asset.displayName = json::Read<std::string>(node["displayName"], asset.id);
            asset.relativePath = NormalizeRelativePath(json::Read<std::string>(node["path"], ""));
            asset.url = json::Read<std::string>(node["url"], "");
            asset.license = json::Read<std::string>(node["license"], "");
            asset.sha256 = ToLowerAscii(json::Read<std::string>(node["sha256"], ""));
            const Json::Value& filesNode = node["files"];
            if (filesNode.isArray())
            {
                for (const Json::Value& fileNode : filesNode)
                {
                    if (!fileNode.isObject())
                    {
                        continue;
                    }
                    SceneFile file;
                    file.relativePath = NormalizeRelativePath(json::Read<std::string>(fileNode["path"], ""));
                    file.sha256 = ToLowerAscii(json::Read<std::string>(fileNode["sha256"], ""));
                    file.sizeBytes = static_cast<uint64_t>(json::Read<int>(fileNode["sizeBytes"], 0));
                    if (!file.relativePath.empty() && !file.sha256.empty())
                    {
                        asset.files.push_back(std::move(file));
                    }
                }
            }
            if (asset.files.empty() && !asset.relativePath.empty() && !asset.sha256.empty())
            {
                asset.files.push_back(SceneFile{asset.relativePath, asset.sha256, 0});
            }
            return asset;
        }

        ResolvedScene MakeResolved(const SceneAsset& asset)
        {
            ResolvedScene resolved;
            resolved.id = asset.id;
            resolved.displayName = asset.displayName;
            resolved.relativePath = asset.relativePath;
            resolved.virtualPath = "/scenes/" + asset.relativePath;
            resolved.expectedSha256 = asset.sha256;
            resolved.url = asset.url;
            resolved.license = asset.license;
            resolved.files = asset.files;
            resolved.integrityRequired = !asset.sha256.empty() || !asset.files.empty();
            return resolved;
        }
    }

    bool SceneCatalog::Load(
        vfs::IFileSystem& fs,
        const std::filesystem::path& manifestVirtualPath,
        SceneCatalog& catalog,
        std::string& error)
    {
        Json::Value root;
        if (!json::LoadFromFile(fs, manifestVirtualPath, root) || !root.isObject())
        {
            error =
                "Could not read scenes/manifest.json. Restore it from Git or rebuild so "
                "scenes/ is copied next to the executable.";
            return false;
        }

        catalog.m_defaultSceneId = json::Read<std::string>(root["defaultSceneId"], "cesium-milk-truck");

        const Json::Value& cameraNode = root["cameraPreset"];
        if (cameraNode.isObject())
        {
            catalog.m_camera.name = json::Read<std::string>(cameraNode["name"], catalog.m_camera.name);
            catalog.m_camera.position = ReadFloat3(cameraNode["position"], catalog.m_camera.position);
            catalog.m_camera.target = ReadFloat3(cameraNode["target"], catalog.m_camera.target);
            catalog.m_camera.up = ReadFloat3(cameraNode["up"], catalog.m_camera.up);
            catalog.m_camera.verticalFovDegrees =
                json::Read<float>(cameraNode["verticalFovDegrees"], catalog.m_camera.verticalFovDegrees);
            catalog.m_camera.zNear = json::Read<float>(cameraNode["zNear"], catalog.m_camera.zNear);
        }

        catalog.m_scenes.clear();
        const Json::Value& scenesNode = root["scenes"];
        if (scenesNode.isArray())
        {
            for (const Json::Value& sceneNode : scenesNode)
            {
                if (!sceneNode.isObject())
                {
                    continue;
                }
                SceneAsset asset = ReadSceneAsset(sceneNode);
                if (asset.id.empty() || asset.relativePath.empty())
                {
                    error = "scenes/manifest.json contains a scene entry without id or path.";
                    return false;
                }
                catalog.m_scenes.push_back(std::move(asset));
            }
        }

        if (!catalog.GetDefaultScene())
        {
            error = "scenes/manifest.json does not define the default scene.";
            return false;
        }

        return true;
    }

    const SceneAsset* SceneCatalog::FindById(std::string_view id) const
    {
        for (const SceneAsset& asset : m_scenes)
        {
            if (asset.id == id)
            {
                return &asset;
            }
        }
        return nullptr;
    }

    const SceneAsset* SceneCatalog::FindByRelativePath(std::string_view relativePath) const
    {
        const std::string normalized = NormalizeRelativePath(relativePath);
        for (const SceneAsset& asset : m_scenes)
        {
            if (asset.relativePath == normalized)
            {
                return &asset;
            }
        }
        return nullptr;
    }

    const SceneAsset* SceneCatalog::GetDefaultScene() const
    {
        if (const SceneAsset* byId = FindById(m_defaultSceneId))
        {
            return byId;
        }
        for (const SceneAsset& asset : m_scenes)
        {
            if (asset.role == "default")
            {
                return &asset;
            }
        }
        return m_scenes.empty() ? nullptr : &m_scenes.front();
    }

    const SceneAsset* SceneCatalog::GetFallbackScene() const
    {
        for (const SceneAsset& asset : m_scenes)
        {
            if (asset.role == "fallback")
            {
                return &asset;
            }
        }
        return FindById("fallback-boxes");
    }

    bool SceneCatalog::Resolve(
        const std::optional<std::string>& sceneArgument,
        ResolvedScene& resolved,
        std::string& error) const
    {
        if (!sceneArgument.has_value() || sceneArgument->empty())
        {
            const SceneAsset* asset = GetDefaultScene();
            if (!asset)
            {
                error = "No default scene is recorded in scenes/manifest.json.";
                return false;
            }
            resolved = MakeResolved(*asset);
            return true;
        }

        const std::filesystem::path argumentPath(*sceneArgument);
        if (LooksAbsolute(argumentPath))
        {
            error =
                "--scene must be a scene id or a path relative to scenes/, not an absolute path.";
            return false;
        }

        if (const SceneAsset* byId = FindById(*sceneArgument))
        {
            resolved = MakeResolved(*byId);
            return true;
        }

        const std::string relative = NormalizeRelativePath(*sceneArgument);
        if (relative.empty() || relative.find("..") != std::string::npos)
        {
            error = "--scene path is empty or escapes the scenes/ directory.";
            return false;
        }

        if (const SceneAsset* byPath = FindByRelativePath(relative))
        {
            resolved = MakeResolved(*byPath);
            return true;
        }

        resolved = {};
        resolved.id = relative;
        resolved.displayName = relative;
        resolved.relativePath = relative;
        resolved.virtualPath = "/scenes/" + relative;
        resolved.integrityRequired = false;
        return true;
    }

    std::string ComputeSha256Hex(const void* data, size_t size)
    {
        BCRYPT_ALG_HANDLE algorithm = nullptr;
        NTSTATUS status = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
        if (status < 0 || !algorithm)
        {
            return {};
        }

        UCHAR hash[32] = {};
        status = BCryptHash(
            algorithm,
            nullptr,
            0,
            static_cast<PUCHAR>(const_cast<void*>(data)),
            static_cast<ULONG>(size),
            hash,
            static_cast<ULONG>(sizeof(hash)));
        BCryptCloseAlgorithmProvider(algorithm, 0);
        if (status < 0)
        {
            return {};
        }

        char hex[65] = {};
        for (size_t index = 0; index < sizeof(hash); ++index)
        {
            std::snprintf(hex + (index * 2), 3, "%02x", hash[index]);
        }
        return hex;
    }

    bool VerifySceneIntegrity(
        vfs::IFileSystem& fs,
        const ResolvedScene& scene,
        std::string& error)
    {
        if (!fs.fileExists(scene.virtualPath))
        {
            std::ostringstream message;
            message << "Missing scene asset '" << scene.virtualPath << "'.";
            if (scene.id == "cesium-milk-truck" || scene.relativePath.find("CesiumMilkTruck") != std::string::npos)
            {
                message << " This file is committed under scenes/cesium-milk-truck/. "
                           "Restore it from Git, then rebuild so scenes/ is copied next to the executable.";
                if (!scene.expectedSha256.empty())
                {
                    message << " Expected SHA-256: " << scene.expectedSha256 << ".";
                }
                message << " A tiny committed fallback is available with --scene fallback-boxes.";
            }
            else if (scene.id == "fallback-boxes")
            {
                message << " Restore scenes/fallback/boxes.gltf from Git.";
            }
            else
            {
                message << " Expected a file under scenes/. The default scene is "
                           "'cesium-milk-truck'. The committed fallback is '--scene fallback-boxes'.";
            }
            error = message.str();
            return false;
        }

        std::vector<SceneFile> files = scene.files;
        if (files.empty() && scene.integrityRequired)
        {
            files.push_back(SceneFile{scene.relativePath, scene.expectedSha256, 0});
        }

        if (files.empty())
        {
            log::warning(
                "Scene '%s' is not listed in scenes/manifest.json; SHA-256 was not verified.",
                scene.virtualPath.c_str());
            return true;
        }

        for (const SceneFile& file : files)
        {
            const std::string virtualPath = "/scenes/" + file.relativePath;
            if (!fs.fileExists(virtualPath))
            {
                error = "Missing scene asset '" + virtualPath + "' listed in scenes/manifest.json.";
                return false;
            }

            const std::shared_ptr<vfs::IBlob> blob = fs.readFile(virtualPath);
            if (!blob || vfs::IBlob::isEmpty(blob.get()))
            {
                error = "Could not read scene asset '" + virtualPath + "' for SHA-256 verification.";
                return false;
            }

            const std::string actual = ComputeSha256Hex(blob->data(), blob->size());
            if (actual.empty())
            {
                error = "Failed to compute SHA-256 for '" + virtualPath + "'.";
                return false;
            }

            if (actual != file.sha256)
            {
                std::ostringstream message;
                message << "Scene asset '" << virtualPath
                        << "' failed SHA-256 verification. Expected " << file.sha256
                        << ", actual " << actual
                        << ". The file is corrupt or is not the revision recorded in scenes/manifest.json.";
                error = message.str();
                return false;
            }
        }

        return true;
    }
}
