find_program(
    POWERSHELL_EXECUTABLE
    NAMES powershell.exe powershell
    REQUIRED
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
        TIMEOUT 15
)
