cmake_minimum_required(VERSION 3.25)

project(
    RenderLab
    VERSION 0.1.0
    DESCRIPTION "A Donut and NVRHI based rendering architecture laboratory"
    LANGUAGES CXX
)

if(NOT WIN32)
    message(FATAL_ERROR "RenderLab currently supports Windows only.")
endif()

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "RenderLab requires a 64-bit toolchain.")
endif()

if(NOT MSVC)
    message(FATAL_ERROR "RenderLab currently requires MSVC.")
endif()

if(MSVC_VERSION LESS 1938)
    message(FATAL_ERROR "RenderLab requires MSVC 19.38 or newer.")
endif()

option(RENDERLAB_BUILD_APPLICATION "Build the Donut/NVRHI application" OFF)

include(cmake/ProjectOptions.cmake)

if(RENDERLAB_BUILD_APPLICATION)
    message(
        FATAL_ERROR
        "The application target is intentionally disabled until implementation step S0.2 "
        "pins and integrates Donut/NVRHI. See IMPLEMENTATION_PLAN.md."
    )
endif()

message(STATUS "RenderLab planning baseline configured successfully.")
