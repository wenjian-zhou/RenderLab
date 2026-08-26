include("${PROJECT_SOURCE_DIR}/external/donut/compileshaders.cmake")

set(RENDERLAB_SHADER_HEADERS
    shaders/renderer_cb.h
    shaders/gbuffer_encoding.hlsli
    shaders/gbuffer_pass.hlsli
    shaders/gbuffer_debug_cb.h
    shaders/lighting_cb.h
    shaders/lighting_debug_cb.h
    shaders/lighting.hlsli
)

set(RENDERLAB_SHADER_SOURCES
    ${RENDERLAB_SHADER_HEADERS}
    shaders/gbuffer_vs.hlsl
    shaders/gbuffer_ps.hlsl
    shaders/gbuffer_debug_vs.hlsl
    shaders/gbuffer_debug_ps.hlsl
    shaders/Shaders.cfg
)

donut_compile_shaders(
    TARGET renderlab_shaders
    PROJECT_NAME RenderLab
    CONFIG "${CMAKE_CURRENT_SOURCE_DIR}/shaders/Shaders.cfg"
    FOLDER RenderLab
    DXIL "${CMAKE_BINARY_DIR}/shaders/dxil"
    OUTPUT_FORMAT BINARY
    SHADER_MODEL 6_5
    SOURCES ${RENDERLAB_SHADER_SOURCES}
    INCLUDES "${CMAKE_CURRENT_SOURCE_DIR}/shaders"
    IGNORE_INCLUDES
        donut/core/math/math.h
        cstdint
)

set(RENDERLAB_RENDERER_SOURCES
    renderer/GBufferContract.h
    renderer/LightingContract.h
    renderer/GBufferTargets.h
    renderer/GBufferTargets.cpp
    renderer/GBufferPass.h
    renderer/GBufferPass.cpp
    renderer/GBufferDebugPass.h
    renderer/GBufferDebugPass.cpp
    renderer/RendererData.h
    renderer/RendererData.cpp
    ${RENDERLAB_SHADER_HEADERS}
)

add_library(RenderLabRenderer STATIC ${RENDERLAB_RENDERER_SOURCES})
add_library(RenderLab::Renderer ALIAS RenderLabRenderer)

target_include_directories(RenderLabRenderer PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

target_link_libraries(
    RenderLabRenderer
    PUBLIC
        RenderLab::ProjectOptions
        donut_engine
        donut_core
        nvrhi
)

set_target_properties(
    RenderLabRenderer
    PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
        FOLDER RenderLab
)

if(MSVC)
    target_compile_options(RenderLabRenderer PRIVATE /wd4100)
endif()

set(RENDERLAB_APP_SOURCES
    app/main.cpp
    app/FrameMarkers.cpp
    app/FrameMarkers.h
    app/RenderingLabApp.cpp
    app/RenderingLabApp.h
    app/SceneCatalog.cpp
    app/SceneCatalog.h
)

add_executable(RenderLab ${RENDERLAB_APP_SOURCES})

target_link_libraries(
    RenderLab
    PRIVATE
        RenderLab::Renderer
        donut_app
        bcrypt
)

add_custom_command(
    TARGET RenderLab POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/scenes"
        "$<TARGET_FILE_DIR:RenderLab>/scenes"
    COMMENT "Copying RenderLab scenes next to the executable"
)

if(MSVC)
    target_compile_options(RenderLab PRIVATE /wd4100)
endif()

add_dependencies(RenderLab donut_shaders renderlab_shaders)

set_target_properties(
    RenderLab
    PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
        VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:RenderLab>"
        FOLDER RenderLab
)

set_property(DIRECTORY "${CMAKE_SOURCE_DIR}" PROPERTY VS_STARTUP_PROJECT RenderLab)
