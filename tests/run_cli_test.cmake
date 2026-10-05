if(NOT DEFINED NETSTORE_EXECUTABLE)
    message(FATAL_ERROR "NETSTORE_EXECUTABLE was not provided")
endif()

execute_process(
    COMMAND "${NETSTORE_EXECUTABLE}"
    INPUT_FILE "${TEST_INPUT}"
    OUTPUT_VARIABLE actual_output
    ERROR_VARIABLE error_output
    RESULT_VARIABLE exit_code
)

if(NOT exit_code EQUAL 0)
    message(FATAL_ERROR
        "NetStore exited with code ${exit_code}: ${error_output}"
    )
endif()

file(READ "${EXPECTED_OUTPUT}" expected_output)

if(NOT actual_output STREQUAL expected_output)
    message(FATAL_ERROR
        "CLI output did not match.\n"
        "Expected:\n${expected_output}\n"
        "Actual:\n${actual_output}"
    )
endif()
