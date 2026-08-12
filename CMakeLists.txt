cmake_minimum_required(VERSION 3.25)

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
