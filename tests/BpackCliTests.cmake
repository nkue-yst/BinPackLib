set(TEST_ROOT "${CMAKE_CURRENT_BINARY_DIR}/BpackCliTestData")
file(REMOVE_RECURSE "${TEST_ROOT}")
file(MAKE_DIRECTORY "${TEST_ROOT}/assets/sub" "${TEST_ROOT}/out")

file(WRITE "${TEST_ROOT}/assets/sub/hello.txt" "hello hello hello hello hello hello hello hello\n")

execute_process(
    COMMAND "${BPACK_EXE}" pack "${TEST_ROOT}/out/test.bpack" --base "${TEST_ROOT}/assets" --overwrite --compress "${TEST_ROOT}/assets"
    RESULT_VARIABLE pack_result
)
if(NOT pack_result EQUAL 0)
    message(FATAL_ERROR "bpack pack failed")
endif()

execute_process(
    COMMAND "${BPACK_EXE}" list "${TEST_ROOT}/out/test.bpack"
    RESULT_VARIABLE list_result
    OUTPUT_VARIABLE list_output
)
if(NOT list_result EQUAL 0)
    message(FATAL_ERROR "bpack list failed")
endif()
if(NOT list_output MATCHES "sub/hello.txt")
    message(FATAL_ERROR "bpack list output did not contain expected virtual path")
endif()

execute_process(
    COMMAND "${BPACK_EXE}" verify "${TEST_ROOT}/out/test.bpack"
    RESULT_VARIABLE verify_result
)
if(NOT verify_result EQUAL 0)
    message(FATAL_ERROR "bpack verify failed")
endif()

execute_process(
    COMMAND "${BPACK_EXE}" extract "${TEST_ROOT}/out/test.bpack" "sub/hello.txt" "${TEST_ROOT}/out/hello.txt"
    RESULT_VARIABLE extract_result
)
if(NOT extract_result EQUAL 0)
    message(FATAL_ERROR "bpack extract failed")
endif()

file(READ "${TEST_ROOT}/assets/sub/hello.txt" original_data)
file(READ "${TEST_ROOT}/out/hello.txt" extracted_data)
if(NOT original_data STREQUAL extracted_data)
    message(FATAL_ERROR "bpack extract output did not match original data")
endif()
