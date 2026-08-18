add_library(RenderLabProjectOptions INTERFACE)
add_library(RenderLab::ProjectOptions ALIAS RenderLabProjectOptions)

target_compile_features(RenderLabProjectOptions INTERFACE cxx_std_20)

target_compile_options(
    RenderLabProjectOptions
    INTERFACE
        $<$<CXX_COMPILER_ID:MSVC>:/W4>
        $<$<CXX_COMPILER_ID:MSVC>:/permissive->
        $<$<CXX_COMPILER_ID:MSVC>:/utf-8>
        $<$<CXX_COMPILER_ID:MSVC>:/Zc:__cplusplus>
)

target_compile_definitions(
    RenderLabProjectOptions
    INTERFACE
        NOMINMAX
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
)

set_target_properties(RenderLabProjectOptions PROPERTIES CXX_EXTENSIONS OFF)
