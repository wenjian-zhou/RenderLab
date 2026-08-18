set(RENDERLAB_DONUT_DIR "${PROJECT_SOURCE_DIR}/external/donut")
set(RENDERLAB_DONUT_COMMIT "bfdebdd7dd5455c503b2737a1967a4ef651c145b")
set(RENDERLAB_NVRHI_COMMIT "8e8c36e37558acec333204619b95d9d2fcdc4a79")
set(RENDERLAB_DIRECTX_HEADERS_COMMIT "d873b344dc540898868697245f100c2a67fe68d9")

if(NOT EXISTS "${RENDERLAB_DONUT_DIR}/CMakeLists.txt")
    message(
        FATAL_ERROR
        "Donut is missing at ${RENDERLAB_DONUT_DIR}. "
        "Run: powershell -NoProfile -File scripts/bootstrap.ps1"
    )
endif()

find_package(Git QUIET)
if(GIT_FOUND)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" rev-parse HEAD
        WORKING_DIRECTORY "${RENDERLAB_DONUT_DIR}"
        OUTPUT_VARIABLE RENDERLAB_DONUT_HEAD
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE RENDERLAB_DONUT_REV_PARSE
    )
    if(NOT RENDERLAB_DONUT_REV_PARSE EQUAL 0)
        message(FATAL_ERROR "Failed to read the Donut submodule revision.")
    endif()
    if(NOT RENDERLAB_DONUT_HEAD STREQUAL RENDERLAB_DONUT_COMMIT)
        message(
            FATAL_ERROR
            "Donut submodule is ${RENDERLAB_DONUT_HEAD}, expected "
            "${RENDERLAB_DONUT_COMMIT}. Run: powershell -NoProfile -File scripts/bootstrap.ps1"
        )
    endif()

    execute_process(
        COMMAND "${GIT_EXECUTABLE}" rev-parse HEAD
        WORKING_DIRECTORY "${RENDERLAB_DONUT_DIR}/nvrhi"
        OUTPUT_VARIABLE RENDERLAB_NVRHI_HEAD
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE RENDERLAB_NVRHI_REV_PARSE
    )
    if(NOT RENDERLAB_NVRHI_REV_PARSE EQUAL 0)
        message(FATAL_ERROR "Failed to read the NVRHI submodule revision.")
    endif()
    if(NOT RENDERLAB_NVRHI_HEAD STREQUAL RENDERLAB_NVRHI_COMMIT)
        message(
            FATAL_ERROR
            "NVRHI submodule is ${RENDERLAB_NVRHI_HEAD}, expected "
            "${RENDERLAB_NVRHI_COMMIT}. Run: powershell -NoProfile -File scripts/bootstrap.ps1"
        )
    endif()
endif()

set(DONUT_WITH_NVRHI ON CACHE BOOL "Enable NVRHI and related Donut projects" FORCE)
set(DONUT_WITH_DX12 ON CACHE BOOL "Enable the DX12 version of Donut" FORCE)
set(DONUT_WITH_DX11 OFF CACHE BOOL "Enable the DX11 version of Donut" FORCE)
set(DONUT_WITH_VULKAN OFF CACHE BOOL "Enable the Vulkan version of Donut" FORCE)
set(DONUT_WITH_AFTERMATH OFF CACHE BOOL "Enable Aftermath crash dump generation with Donut" FORCE)
set(DONUT_WITH_STREAMLINE OFF CACHE BOOL "Enable Streamline" FORCE)
set(DONUT_WITH_DLSS OFF CACHE BOOL "Enable NGX DLSS integration" FORCE)
set(DONUT_WITH_AUDIO OFF CACHE BOOL "Include Donut audio features" FORCE)
set(DONUT_WITH_UNIT_TESTS OFF CACHE BOOL "Donut unit tests" FORCE)
set(DONUT_WITH_STATIC_SHADERS OFF CACHE BOOL "Build Donut with statically linked shaders" FORCE)

set(NVRHI_WITH_DX12 ON CACHE BOOL "Build the NVRHI D3D12 backend" FORCE)
set(NVRHI_WITH_DX11 OFF CACHE BOOL "Build the NVRHI D3D11 backend" FORCE)
set(NVRHI_WITH_VULKAN OFF CACHE BOOL "Build the NVRHI Vulkan backend" FORCE)
set(NVRHI_WITH_VALIDATION ON CACHE BOOL "Build the NVRHI validation layer" FORCE)
set(NVRHI_WITH_RTXMU OFF CACHE BOOL "Use RTXMU for acceleration structure management" FORCE)
set(NVRHI_WITH_NVAPI OFF CACHE BOOL "Include NVAPI support" FORCE)
set(NVRHI_WITH_AFTERMATH OFF CACHE BOOL "Include Aftermath support" FORCE)
set(NVRHI_FETCH_DIRECTX_HEADERS ON CACHE BOOL "Fetch DirectX-Headers for the D3D12 backend" FORCE)
set(NVRHI_DIRECTX_HEADERS_GIT_TAG "${RENDERLAB_DIRECTX_HEADERS_COMMIT}" CACHE STRING "DirectX-Headers git tag or commit" FORCE)
set(NVRHI_INSTALL OFF CACHE BOOL "Generate install rules for NVRHI" FORCE)

set(DONUT_SHADERS_OUTPUT_DIR "${CMAKE_BINARY_DIR}/shaders" CACHE PATH "Donut compiled shader output" FORCE)

set(SHADERMAKE_FIND_DXC ON CACHE BOOL "Download the pinned DXC build" FORCE)
set(SHADERMAKE_FIND_FXC OFF CACHE BOOL "Find FXC in the Windows SDK" FORCE)
set(SHADERMAKE_FIND_DXC_VK OFF CACHE BOOL "Find DXC in the Vulkan SDK" FORCE)
set(SHADERMAKE_FIND_SLANG OFF CACHE BOOL "Download Slang" FORCE)
set(SHADERMAKE_DXC_VERSION "v1.9.2602" CACHE STRING "DXC release downloaded by ShaderMake" FORCE)
set(SHADERMAKE_DXC_DATE "2026_02_20" CACHE STRING "DXC release date used in the download URL" FORCE)

add_subdirectory("${RENDERLAB_DONUT_DIR}" "${CMAKE_BINARY_DIR}/donut")

if(DONUT_WITH_DX11 OR DONUT_WITH_VULKAN OR NVRHI_WITH_DX11 OR NVRHI_WITH_VULKAN)
    message(
        FATAL_ERROR
        "RenderLab must configure a D3D12-only Donut/NVRHI backend. "
        "DONUT_WITH_DX11=${DONUT_WITH_DX11} DONUT_WITH_VULKAN=${DONUT_WITH_VULKAN} "
        "NVRHI_WITH_DX11=${NVRHI_WITH_DX11} NVRHI_WITH_VULKAN=${NVRHI_WITH_VULKAN}"
    )
endif()

if(NOT DONUT_WITH_DX12 OR NOT NVRHI_WITH_DX12)
    message(FATAL_ERROR "RenderLab requires DONUT_WITH_DX12 and NVRHI_WITH_DX12.")
endif()

if(NOT NVRHI_DIRECTX_HEADERS_GIT_TAG STREQUAL RENDERLAB_DIRECTX_HEADERS_COMMIT)
    message(
        FATAL_ERROR
        "NVRHI_DIRECTX_HEADERS_GIT_TAG is ${NVRHI_DIRECTX_HEADERS_GIT_TAG}, "
        "expected ${RENDERLAB_DIRECTX_HEADERS_COMMIT}."
    )
endif()

message(STATUS "RenderLab graphics backend: D3D12")
message(STATUS "Donut commit: ${RENDERLAB_DONUT_COMMIT}")
message(STATUS "NVRHI commit: ${RENDERLAB_NVRHI_COMMIT}")
message(STATUS "DONUT_WITH_DX11=${DONUT_WITH_DX11}")
message(STATUS "DONUT_WITH_DX12=${DONUT_WITH_DX12}")
message(STATUS "DONUT_WITH_VULKAN=${DONUT_WITH_VULKAN}")
message(STATUS "NVRHI_WITH_DX11=${NVRHI_WITH_DX11}")
message(STATUS "NVRHI_WITH_DX12=${NVRHI_WITH_DX12}")
message(STATUS "NVRHI_WITH_VULKAN=${NVRHI_WITH_VULKAN}")
message(STATUS "NVRHI_DIRECTX_HEADERS_GIT_TAG=${NVRHI_DIRECTX_HEADERS_GIT_TAG}")
