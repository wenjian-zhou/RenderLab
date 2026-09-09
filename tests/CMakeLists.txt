add_executable(
    RenderLabDataContractTests
    test_renderer_data.cpp
    test_gbuffer_targets.cpp
    test_gbuffer_pass.cpp
    test_gbuffer_debug.cpp
    test_lighting_contract.cpp
    test_lighting_debug.cpp
    test_image_compare.cpp
    test_hdr_compare.cpp
    test_postprocess_contract.cpp
    test_postprocess_pass.cpp
    test_rdg_handles.cpp
    test_rdg_declarations.cpp
    test_rdg_versioning.cpp
    image_compare.cpp
    image_compare.h
    hdr_compare.cpp
    hdr_compare.h
)

target_link_libraries(
    RenderLabDataContractTests
    PRIVATE
        RenderLab::Renderer
        RenderLab::Rdg
        stb
        jsoncpp_static
)

file(TO_CMAKE_PATH "${CMAKE_SOURCE_DIR}" RENDERLAB_SOURCE_DIR_CMAKE)
target_compile_definitions(
    RenderLabDataContractTests
    PRIVATE
        RENDERLAB_SOURCE_DIR="${RENDERLAB_SOURCE_DIR_CMAKE}"
)

if(MSVC)
    target_compile_options(RenderLabDataContractTests PRIVATE /wd4100)
endif()

set_target_properties(
    RenderLabDataContractTests
    PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
        FOLDER RenderLab
)

add_test(NAME RenderLabDataContractTests COMMAND RenderLabDataContractTests)

add_executable(
    RenderLabGoldenCompare
    golden_compare_main.cpp
    image_compare.cpp
    image_compare.h
    hdr_compare.cpp
    hdr_compare.h
    stb_image_impl.cpp
)

target_link_libraries(
    RenderLabGoldenCompare
    PRIVATE
        stb
        jsoncpp_static
)

target_include_directories(RenderLabGoldenCompare PRIVATE "${CMAKE_SOURCE_DIR}/src")

if(MSVC)
    target_compile_options(RenderLabGoldenCompare PRIVATE /wd4100)
endif()

set_target_properties(
    RenderLabGoldenCompare
    PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
        FOLDER RenderLab
)
