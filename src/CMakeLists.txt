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
