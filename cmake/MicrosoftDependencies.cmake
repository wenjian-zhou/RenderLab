include_guard(GLOBAL)

set(RENDERLAB_NUGET_ROOT "${CMAKE_SOURCE_DIR}/external/nuget")

if(NOT EXISTS "${CMAKE_SOURCE_DIR}/external/versions.json")
    message(FATAL_ERROR "external/versions.json is missing. Run scripts/bootstrap.ps1 before configuring.")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/AgilitySDK.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/DXC.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/WinPixEventRuntime.cmake")

function(renderlab_stage_microsoft_runtimes target)
    renderlab_stage_agility_sdk("${target}")
    renderlab_stage_dxc("${target}")
    renderlab_stage_winpix_event_runtime("${target}")
endfunction()
