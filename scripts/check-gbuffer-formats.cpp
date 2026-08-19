// One-shot S1.1 format probe. Not part of the RenderLab application.
// Queries D3D12 format support for the frozen GBuffer contract on the preferred adapter.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <cstdio>
#include <cstdint>
#include <string>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

using Microsoft::WRL::ComPtr;

namespace
{
    bool IsSoftwareOrVirtualAdapter(IDXGIAdapter1* adapter)
    {
        DXGI_ADAPTER_DESC1 desc = {};
        if (FAILED(adapter->GetDesc1(&desc)))
        {
            return true;
        }
        if (desc.VendorId == 0x1414 || (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE))
        {
            return true;
        }
        return false;
    }

    std::string Narrow(const wchar_t* wide)
    {
        if (!wide)
        {
            return "unknown";
        }
        char buffer[256] = {};
        const int count = WideCharToMultiByte(CP_UTF8, 0, wide, -1, buffer, sizeof(buffer) - 1, nullptr, nullptr);
        return count > 0 ? std::string(buffer) : "unknown";
    }

    const char* YesNo(bool value)
    {
        return value ? "yes" : "NO";
    }

    struct FormatQuery
    {
        DXGI_FORMAT format;
        const char* name;
        bool needTexture = false;
        bool needRenderTarget = false;
        bool needShaderLoad = false;
        bool needShaderSample = false;
        bool needDepthStencil = false;
    };

    bool QueryFormat(
        ID3D12Device* device,
        const FormatQuery& query,
        bool& ok)
    {
        D3D12_FEATURE_DATA_FORMAT_SUPPORT data = {};
        data.Format = query.format;
        const HRESULT hr = device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &data, sizeof(data));
        if (FAILED(hr))
        {
            std::printf("  %-32s CheckFeatureSupport failed (0x%08X)\n", query.name, static_cast<unsigned>(hr));
            ok = false;
            return false;
        }

        const bool texture = (data.Support1 & D3D12_FORMAT_SUPPORT1_TEXTURE2D) != 0;
        const bool renderTarget = (data.Support1 & D3D12_FORMAT_SUPPORT1_RENDER_TARGET) != 0;
        const bool shaderLoad = (data.Support1 & D3D12_FORMAT_SUPPORT1_SHADER_LOAD) != 0;
        const bool shaderSample = (data.Support1 & D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE) != 0;
        const bool depthStencil = (data.Support1 & D3D12_FORMAT_SUPPORT1_DEPTH_STENCIL) != 0;

        const bool pass =
            (!query.needTexture || texture) &&
            (!query.needRenderTarget || renderTarget) &&
            (!query.needShaderLoad || shaderLoad) &&
            (!query.needShaderSample || shaderSample) &&
            (!query.needDepthStencil || depthStencil);

        std::printf(
            "  %-32s tex2D=%s RTV=%s SRV-load=%s SRV-sample=%s DSV=%s  [%s]\n",
            query.name,
            YesNo(texture),
            YesNo(renderTarget),
            YesNo(shaderLoad),
            YesNo(shaderSample),
            YesNo(depthStencil),
            pass ? "ok" : "FAIL");

        if (!pass)
        {
            ok = false;
        }
        return pass;
    }

    bool TryCreateDepthWithSrv(ID3D12Device* device)
    {
        D3D12_HEAP_PROPERTIES heap = {};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;

        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = 64;
        desc.Height = 64;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_R32_TYPELESS;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

        D3D12_CLEAR_VALUE clear = {};
        clear.Format = DXGI_FORMAT_D32_FLOAT;
        clear.DepthStencil.Depth = 0.0f;

        ComPtr<ID3D12Resource> resource;
        const HRESULT hr = device->CreateCommittedResource(
            &heap,
            D3D12_HEAP_FLAG_NONE,
            &desc,
            D3D12_RESOURCE_STATE_DEPTH_WRITE,
            &clear,
            IID_PPV_ARGS(&resource));
        if (FAILED(hr))
        {
            std::printf("  CreateCommittedResource R32_TYPELESS DEPTH_STENCIL failed (0x%08X)\n", static_cast<unsigned>(hr));
            return false;
        }

        D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
        heapDesc.NumDescriptors = 2;
        heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        ComPtr<ID3D12DescriptorHeap> dsvHeap;
        if (FAILED(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&dsvHeap))))
        {
            std::printf("  CreateDescriptorHeap DSV failed\n");
            return false;
        }

        D3D12_DEPTH_STENCIL_VIEW_DESC dsv = {};
        dsv.Format = DXGI_FORMAT_D32_FLOAT;
        dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        device->CreateDepthStencilView(resource.Get(), &dsv, dsvHeap->GetCPUDescriptorHandleForHeapStart());

        heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        ComPtr<ID3D12DescriptorHeap> srvHeap;
        if (FAILED(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&srvHeap))))
        {
            std::printf("  CreateDescriptorHeap SRV failed\n");
            return false;
        }

        D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
        srv.Format = DXGI_FORMAT_R32_FLOAT;
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.Texture2D.MipLevels = 1;
        device->CreateShaderResourceView(resource.Get(), &srv, srvHeap->GetCPUDescriptorHandleForHeapStart());

        std::printf("  R32_TYPELESS texture with D32_FLOAT DSV + R32_FLOAT SRV: ok\n");
        return true;
    }
}

int main()
{
    ComPtr<IDXGIFactory6> factory;
    if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory))))
    {
        std::fprintf(stderr, "error: CreateDXGIFactory2 failed\n");
        return 1;
    }

    ComPtr<IDXGIAdapter1> chosen;
    DXGI_ADAPTER_DESC1 chosenDesc = {};
    SIZE_T bestNvidiaMemory = 0;
    SIZE_T bestMemory = 0;
    ComPtr<IDXGIAdapter1> bestAny;

    for (UINT index = 0;; ++index)
    {
        ComPtr<IDXGIAdapter1> adapter;
        if (factory->EnumAdapters1(index, &adapter) == DXGI_ERROR_NOT_FOUND)
        {
            break;
        }
        if (IsSoftwareOrVirtualAdapter(adapter.Get()))
        {
            continue;
        }

        DXGI_ADAPTER_DESC1 desc = {};
        adapter->GetDesc1(&desc);
        std::printf("Adapter %u: %s (%u MB)\n", index, Narrow(desc.Description).c_str(),
                    static_cast<unsigned>(desc.DedicatedVideoMemory / (1024 * 1024)));

        if (desc.DedicatedVideoMemory >= bestMemory)
        {
            bestMemory = desc.DedicatedVideoMemory;
            bestAny = adapter;
        }
        if (desc.VendorId == 0x10DE && desc.DedicatedVideoMemory >= bestNvidiaMemory)
        {
            bestNvidiaMemory = desc.DedicatedVideoMemory;
            chosen = adapter;
            chosenDesc = desc;
        }
    }

    if (!chosen)
    {
        chosen = bestAny;
        if (chosen)
        {
            chosen->GetDesc1(&chosenDesc);
        }
    }
    if (!chosen)
    {
        std::fprintf(stderr, "error: no suitable D3D12 adapter\n");
        return 1;
    }

    std::printf("Using adapter: %s\n", Narrow(chosenDesc.Description).c_str());

    ComPtr<ID3D12Device> device;
    if (FAILED(D3D12CreateDevice(chosen.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device))))
    {
        std::fprintf(stderr, "error: D3D12CreateDevice failed\n");
        return 1;
    }

    D3D12_FEATURE_DATA_FEATURE_LEVELS levels = {};
    D3D_FEATURE_LEVEL requested[] = {
        D3D_FEATURE_LEVEL_12_2,
        D3D_FEATURE_LEVEL_12_1,
        D3D_FEATURE_LEVEL_12_0,
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0};
    levels.NumFeatureLevels = _countof(requested);
    levels.pFeatureLevelsRequested = requested;
    device->CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS, &levels, sizeof(levels));
    std::printf("Max feature level: 0x%x\n", static_cast<unsigned>(levels.MaxSupportedFeatureLevel));

    bool ok = true;
    std::printf("GBuffer format support:\n");

    const FormatQuery queries[] = {
        {DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, "GBufferA SRGBA8_UNORM", true, true, true, true, false},
        {DXGI_FORMAT_R16G16B16A16_FLOAT, "GBufferB RGBA16_FLOAT", true, true, true, true, false},
        {DXGI_FORMAT_R8G8B8A8_UNORM, "GBufferC RGBA8_UNORM", true, true, true, true, false},
        {DXGI_FORMAT_D32_FLOAT, "GBufferDepth D32_FLOAT DSV", true, false, false, false, true},
        {DXGI_FORMAT_R32_FLOAT, "GBufferDepth R32_FLOAT SRV", true, false, true, true, false},
    };

    for (const FormatQuery& query : queries)
    {
        QueryFormat(device.Get(), query, ok);
    }

    if (!TryCreateDepthWithSrv(device.Get()))
    {
        ok = false;
    }

    if (!ok)
    {
        std::printf("S1.1 format probe: FAIL\n");
        return 1;
    }

    std::printf("S1.1 format probe: PASS\n");
    return 0;
}
