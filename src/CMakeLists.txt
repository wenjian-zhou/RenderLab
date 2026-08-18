add_library(
    RenderLabD3D12
    STATIC
    gfx/d3d12/adapter_discovery.cpp
    gfx/d3d12/adapter_discovery.h
    gfx/d3d12/adapter_report.cpp
    gfx/d3d12/adapter_report.h
    gfx/d3d12/adapter_selection.cpp
    gfx/d3d12/adapter_selection.h
    gfx/d3d12/device_bootstrap.cpp
    gfx/d3d12/device_bootstrap.h
    gfx/d3d12/device_bootstrap_report.cpp
    gfx/d3d12/device_bootstrap_report.h
    gfx/d3d12/device_feature_report.cpp
    gfx/d3d12/device_feature_report.h
    gfx/d3d12/device_features.cpp
    gfx/d3d12/device_features.h
    gfx/d3d12/depth_stencil_storage.cpp
    gfx/d3d12/depth_stencil_storage.h
    gfx/d3d12/device_removal_report.cpp
    gfx/d3d12/device_removal_report.h
    gfx/d3d12/diagnostics_bootstrap.cpp
    gfx/d3d12/diagnostics_bootstrap.h
    gfx/d3d12/diagnostics_config.h
    gfx/d3d12/diagnostics_report.cpp
    gfx/d3d12/diagnostics_report.h
    gfx/d3d12/dxgi_factory.cpp
    gfx/d3d12/dxgi_factory.h
    gfx/d3d12/hresult_error.cpp
    gfx/d3d12/hresult_error.h
    gfx/d3d12/info_queue_messages.cpp
    gfx/d3d12/info_queue_messages.h
    gfx/d3d12/info_queue_report.cpp
    gfx/d3d12/info_queue_report.h
    gfx/d3d12/window_swap_chain.cpp
    gfx/d3d12/window_swap_chain.h
)

target_link_libraries(
    RenderLabD3D12
    PUBLIC
        RenderLab::AgilitySDK
        d3d12
        dxgi
)

target_compile_features(RenderLabD3D12 PUBLIC cxx_std_20)

target_include_directories(
    RenderLabD3D12
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_options(
    RenderLabD3D12
    PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/W4>
        $<$<CXX_COMPILER_ID:MSVC>:/permissive->
        $<$<CXX_COMPILER_ID:MSVC>:/utf-8>
        $<$<CXX_COMPILER_ID:MSVC>:/Zc:__cplusplus>
)

add_executable(
    RenderLab
    WIN32
    app/main.cpp
    platform/win32/win32_application.cpp
    platform/win32/win32_application.h
)

target_compile_features(RenderLab PRIVATE cxx_std_20)

target_compile_definitions(
    RenderLab
    PRIVATE
        NOMINMAX
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
)

target_include_directories(RenderLab PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(
    RenderLab
    PRIVATE
        RenderLabD3D12
        RenderLab::AgilitySDK
        RenderLab::DXC
        RenderLab::WinPixEventRuntime
        Shell32
        User32
)

target_compile_options(
    RenderLab
    PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/W4>
        $<$<CXX_COMPILER_ID:MSVC>:/permissive->
        $<$<CXX_COMPILER_ID:MSVC>:/utf-8>
        $<$<CXX_COMPILER_ID:MSVC>:/Zc:__cplusplus>
)

set_target_properties(
    RenderLab
    PROPERTIES
        CXX_EXTENSIONS OFF
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

renderlab_stage_microsoft_runtimes(RenderLab)
