if(NOT DEFINED PRINCIPIA_DEMO OR PRINCIPIA_DEMO STREQUAL "")
    message(FATAL_ERROR "PRINCIPIA_DEMO must name the built presentation executable")
endif()

function(expect_rejected expected_diagnostic)
    execute_process(
        COMMAND "${PRINCIPIA_DEMO}" ${ARGN}
        RESULT_VARIABLE result
        OUTPUT_VARIABLE standard_output
        ERROR_VARIABLE standard_error
    )
    if(NOT result STREQUAL "1")
        message(
            FATAL_ERROR
            "Expected exit code 1 for arguments [${ARGN}], received ${result}.\n"
            "stdout: ${standard_output}\nstderr: ${standard_error}"
        )
    endif()
    string(CONCAT combined_output "${standard_output}" "${standard_error}")
    string(FIND "${combined_output}" "${expected_diagnostic}" diagnostic_position)
    if(diagnostic_position EQUAL -1)
        message(
            FATAL_ERROR
            "Missing diagnostic '${expected_diagnostic}' for arguments [${ARGN}].\n"
            "stdout: ${standard_output}\nstderr: ${standard_error}"
        )
    endif()
endfunction()

expect_rejected("--frames requires a positive integer" --frames 10junk)
expect_rejected("--frames requires a positive integer" --frames 0)
expect_rejected("--frames requires a positive integer" --frames -1)
expect_rejected("--frames requires a positive integer" --frames +1)
expect_rejected("--frames requires a positive integer" --frames 1.0)
expect_rejected("--frames requires a positive integer" --frames " 1")
expect_rejected("--frames requires a positive integer" --frames 999999999999999999999999)
expect_rejected("usage: principia_demo" --unknown)
expect_rejected("usage: principia_demo" --frames)
expect_rejected("usage: principia_demo" --frames 10 extra)
