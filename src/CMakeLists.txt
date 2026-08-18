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

target_include_directories(RenderLab PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

target_link_libraries(
    RenderLab
    PRIVATE
        RenderLab::ProjectOptions
        donut_app
        donut_engine
        donut_core
        nvrhi
        bcrypt
)

add_custom_command(
    TARGET RenderLab POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/scenes"
        "$<TARGET_FILE_DIR:RenderLab>/scenes"
    COMMENT "Copying RenderLab scenes next to the executable"
)

# Donut headers contain unused parameters in inline defaults. Keep /W4 on RenderLab
# source while silencing those third-party diagnostics.
if(MSVC)
    target_compile_options(RenderLab PRIVATE /wd4100)
endif()

add_dependencies(RenderLab donut_shaders)

set_target_properties(
    RenderLab
    PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
        VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:RenderLab>"
)

set_property(DIRECTORY "${CMAKE_SOURCE_DIR}" PROPERTY VS_STARTUP_PROJECT RenderLab)
