#pragma once

#include <nvrhi/nvrhi.h>

namespace renderlab::markers
{
    // Stable PIX / NVRHI marker names. These strings are identical in Debug and Release.
    inline constexpr const char* kFrame = "Frame";
    inline constexpr const char* kSceneUpdate = "SceneUpdate";
    inline constexpr const char* kRender = "Render";
    inline constexpr const char* kGBuffer = "GBuffer";
    inline constexpr const char* kDeferredLighting = "DeferredLighting";
    inline constexpr const char* kPostProcess = "PostProcess";
    inline constexpr const char* kGBufferDebug = "GBufferDebug";
    inline constexpr const char* kLightingDebug = "LightingDebug";
    inline constexpr const char* kUI = "UI";
    inline constexpr const char* kPresent = "Present";

    void PrintMarkerNames();

    // CPU / command-queue PIX event. Visible in PIX GPU and timing captures.
    class CpuMarker
    {
    public:
        CpuMarker(nvrhi::IDevice* device, const char* name);
        ~CpuMarker();

        CpuMarker(const CpuMarker&) = delete;
        CpuMarker& operator=(const CpuMarker&) = delete;

    private:
        void* m_queue = nullptr;
    };

    // GPU command-list marker. Maps to PIXBeginEvent on the D3D12 command list.
    class GpuMarker
    {
    public:
        GpuMarker(nvrhi::ICommandList* commandList, const char* name);
        ~GpuMarker();

        GpuMarker(const GpuMarker&) = delete;
        GpuMarker& operator=(const GpuMarker&) = delete;

    private:
        nvrhi::ICommandList* m_commandList = nullptr;
    };

    // Submit a named empty GPU range so Present appears in a PIX event list.
    void EmitStandaloneGpuMarker(
        nvrhi::IDevice* device,
        nvrhi::ICommandList* commandList,
        const char* name);
}
