include_guard(GLOBAL)

set(_renderlab_dxc_root "${RENDERLAB_NUGET_ROOT}/dxc")
set(_renderlab_dxc_include "${_renderlab_dxc_root}/build/native/include")
set(_renderlab_dxc_bin "${_renderlab_dxc_root}/build/native/bin/x64")
set(_renderlab_dxc_lib "${_renderlab_dxc_root}/build/native/lib/x64")

foreach(_required_file IN ITEMS
        "${_renderlab_dxc_include}/dxcapi.h"
        "${_renderlab_dxc_lib}/dxcompiler.lib"
        "${_renderlab_dxc_bin}/dxc.exe"
        "${_renderlab_dxc_bin}/dxcompiler.dll"
        "${_renderlab_dxc_bin}/dxil.dll")
    if(NOT EXISTS "${_required_file}")
        message(FATAL_ERROR "Missing locked DXC file: ${_required_file}. Run scripts/bootstrap.ps1.")
    endif()
endforeach()

add_library(RenderLab_DXC SHARED IMPORTED GLOBAL)
add_library(RenderLab::DXC ALIAS RenderLab_DXC)
set_target_properties(
    RenderLab_DXC
    PROPERTIES
        IMPORTED_IMPLIB "${_renderlab_dxc_lib}/dxcompiler.lib"
        IMPORTED_LOCATION "${_renderlab_dxc_bin}/dxcompiler.dll"
        INTERFACE_INCLUDE_DIRECTORIES "${_renderlab_dxc_include}"
)

set(RENDERLAB_DXC_EXECUTABLE "${_renderlab_dxc_bin}/dxc.exe" CACHE INTERNAL "Locked DXC executable")

function(renderlab_stage_dxc target)
    foreach(_runtime_file IN ITEMS dxcompiler.dll dxil.dll)
        add_custom_command(
            TARGET "${target}"
            POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                    "${_renderlab_dxc_bin}/${_runtime_file}"
                    "$<TARGET_FILE_DIR:${target}>/${_runtime_file}"
            VERBATIM
        )
    endforeach()
endfunction()
