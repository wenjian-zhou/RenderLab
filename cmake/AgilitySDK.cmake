include_guard(GLOBAL)

set(_renderlab_agility_root "${RENDERLAB_NUGET_ROOT}/agility-sdk")
set(_renderlab_agility_include "${_renderlab_agility_root}/build/native/include")
set(_renderlab_agility_bin "${_renderlab_agility_root}/build/native/bin/x64")

foreach(_required_file IN ITEMS
        "${_renderlab_agility_include}/d3d12.h"
        "${_renderlab_agility_include}/d3d12sdklayers.h"
        "${_renderlab_agility_bin}/D3D12Core.dll"
        "${_renderlab_agility_bin}/d3d12SDKLayers.dll")
    if(NOT EXISTS "${_required_file}")
        message(FATAL_ERROR "Missing locked Agility SDK file: ${_required_file}. Run scripts/bootstrap.ps1.")
    endif()
endforeach()

add_library(RenderLab_AgilitySDK INTERFACE)
add_library(RenderLab::AgilitySDK ALIAS RenderLab_AgilitySDK)
target_include_directories(RenderLab_AgilitySDK BEFORE INTERFACE "${_renderlab_agility_include}")

function(renderlab_stage_agility_sdk target)
    add_custom_command(
        TARGET "${target}"
        POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:${target}>/D3D12"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${_renderlab_agility_bin}/D3D12Core.dll"
                "$<TARGET_FILE_DIR:${target}>/D3D12/D3D12Core.dll"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${_renderlab_agility_bin}/d3d12SDKLayers.dll"
                "$<TARGET_FILE_DIR:${target}>/D3D12/d3d12SDKLayers.dll"
        VERBATIM
    )
endfunction()
