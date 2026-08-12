include_guard(GLOBAL)

set(_renderlab_pix_root "${RENDERLAB_NUGET_ROOT}/pix-runtime")
set(_renderlab_pix_include "${_renderlab_pix_root}/Include/WinPixEventRuntime")
set(_renderlab_pix_bin "${_renderlab_pix_root}/bin/x64")

foreach(_required_file IN ITEMS
        "${_renderlab_pix_include}/pix3.h"
        "${_renderlab_pix_bin}/WinPixEventRuntime.lib"
        "${_renderlab_pix_bin}/WinPixEventRuntime.dll")
    if(NOT EXISTS "${_required_file}")
        message(FATAL_ERROR "Missing locked WinPixEventRuntime file: ${_required_file}. Run scripts/bootstrap.ps1.")
    endif()
endforeach()

add_library(RenderLab_WinPixEventRuntime SHARED IMPORTED GLOBAL)
add_library(RenderLab::WinPixEventRuntime ALIAS RenderLab_WinPixEventRuntime)
set_target_properties(
    RenderLab_WinPixEventRuntime
    PROPERTIES
        IMPORTED_IMPLIB "${_renderlab_pix_bin}/WinPixEventRuntime.lib"
        IMPORTED_LOCATION "${_renderlab_pix_bin}/WinPixEventRuntime.dll"
        INTERFACE_INCLUDE_DIRECTORIES "${_renderlab_pix_include}"
)

function(renderlab_stage_winpix_event_runtime target)
    add_custom_command(
        TARGET "${target}"
        POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${_renderlab_pix_bin}/WinPixEventRuntime.dll"
                "$<TARGET_FILE_DIR:${target}>/WinPixEventRuntime.dll"
        VERBATIM
    )
endfunction()
