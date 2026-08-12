find_program(
    POWERSHELL_EXECUTABLE
    NAMES powershell.exe powershell
    REQUIRED
)

find_package(Catch2 3 CONFIG REQUIRED)

add_executable(
    RenderLabCpuTests
    cpu/bootstrap_contract_tests.cpp
)

target_compile_features(RenderLabCpuTests PRIVATE cxx_std_20)
target_link_libraries(RenderLabCpuTests PRIVATE Catch2::Catch2WithMain)

target_compile_options(
    RenderLabCpuTests
    PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/W4>
        $<$<CXX_COMPILER_ID:MSVC>:/permissive->
        $<$<CXX_COMPILER_ID:MSVC>:/utf-8>
        $<$<CXX_COMPILER_ID:MSVC>:/Zc:__cplusplus>
)

set_target_properties(
    RenderLabCpuTests
    PROPERTIES
        CXX_EXTENSIONS OFF
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

include(Catch)
catch_discover_tests(
    RenderLabCpuTests
    TEST_PREFIX "renderlab.cpu."
    PROPERTIES LABELS "cpu"
)

add_test(
    NAME renderlab.window_smoke
    COMMAND
        "${POWERSHELL_EXECUTABLE}"
        -NoProfile
        -ExecutionPolicy Bypass
        -File "${CMAKE_CURRENT_SOURCE_DIR}/window_smoke.ps1"
        -Executable "$<TARGET_FILE:RenderLab>"
)

set_tests_properties(
    renderlab.window_smoke
    PROPERTIES
        LABELS "smoke;windows"
        TIMEOUT 15
)
