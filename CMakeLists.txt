cmake_minimum_required(VERSION 3.25)

set(RENDERLAB_VCPKG_TRIPLET "x64-windows-static-md")

foreach(triplet_variable IN ITEMS VCPKG_TARGET_TRIPLET VCPKG_HOST_TRIPLET)
    if(DEFINED ${triplet_variable}
       AND NOT "${${triplet_variable}}" STREQUAL "${RENDERLAB_VCPKG_TRIPLET}")
        message(
            FATAL_ERROR
            "${triplet_variable} must be ${RENDERLAB_VCPKG_TRIPLET}; "
            "mixing vcpkg triplets is not supported."
        )
    endif()
endforeach()

if(DEFINED VCPKG_OVERLAY_TRIPLETS
   AND NOT EXISTS "${VCPKG_OVERLAY_TRIPLETS}/${RENDERLAB_VCPKG_TRIPLET}.cmake")
    message(
        FATAL_ERROR
        "VCPKG_OVERLAY_TRIPLETS must contain ${RENDERLAB_VCPKG_TRIPLET}.cmake."
    )
endif()

project(
    RenderLab
    VERSION 0.1.0
    DESCRIPTION "A focused D3D12 and DXR rendering laboratory"
    LANGUAGES CXX
)

if(NOT WIN32)
    message(FATAL_ERROR "RenderLab currently supports Windows only.")
endif()

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "RenderLab requires the x64 MSVC toolchain.")
endif()

if(NOT MSVC)
    message(FATAL_ERROR "RenderLab requires the MSVC v143 toolchain.")
endif()

if(NOT MSVC_TOOLSET_VERSION EQUAL 143)
    message(
        FATAL_ERROR
        "RenderLab requires the MSVC v143 toolset; CMake selected "
        "'${MSVC_TOOLSET_VERSION}'. Use the windows-vs2022 preset."
    )
endif()

if(MSVC_VERSION LESS 1938)
    message(FATAL_ERROR "RenderLab requires MSVC 19.38 or newer within the v143 toolset family.")
endif()

if(NOT CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION)
    message(
        FATAL_ERROR
        "CMake did not select a Windows SDK. Install Windows SDK 10.0.22621.0 or newer."
    )
endif()

if(CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION VERSION_LESS "10.0.22621.0")
    message(
        FATAL_ERROR
        "RenderLab requires Windows SDK 10.0.22621.0 or newer; CMake selected "
        "'${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}'."
    )
endif()

include(cmake/MicrosoftDependencies.cmake)

add_subdirectory(src)

include(CTest)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
