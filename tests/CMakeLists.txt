add_executable(
    RenderLabDataContractTests
    test_renderer_data.cpp
    test_gbuffer_targets.cpp
    test_gbuffer_pass.cpp
    test_gbuffer_debug.cpp
)

target_link_libraries(
    RenderLabDataContractTests
    PRIVATE
        RenderLab::Renderer
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
