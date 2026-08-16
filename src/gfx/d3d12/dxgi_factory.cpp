#include "gfx/d3d12/dxgi_factory.h"

namespace renderlab::gfx::d3d12
{
HRESULT CreateDxgiFactory(
    bool enableDebugLayer,
    Microsoft::WRL::ComPtr<IDXGIFactory6> &factory) noexcept
{
    factory.Reset();

    const UINT flags =
        enableDebugLayer ? DXGI_CREATE_FACTORY_DEBUG : 0;

    return CreateDXGIFactory2(
        flags,
        IID_PPV_ARGS(&factory));
}
} // namespace renderlab::gfx::d3d12
