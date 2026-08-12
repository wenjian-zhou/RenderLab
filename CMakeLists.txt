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

add_subdirectory(src)

option(BUILD_TESTING "Build RenderLab tests" ON)

if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()
