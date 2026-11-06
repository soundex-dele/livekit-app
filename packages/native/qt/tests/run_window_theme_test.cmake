# QtTest can route Windows output away from CTest's console. Forward its file
# logger so assertions and capability skips remain visible in LastTest.log.
file(WRITE "${TEST_LOG}" "")
execute_process(
  COMMAND "${TEST_EXECUTABLE}" -o "${TEST_LOG},txt"
  RESULT_VARIABLE test_result)
file(READ "${TEST_LOG}" test_output)
message("${test_output}")
if(NOT "${test_result}" STREQUAL "0")
  message(FATAL_ERROR "Native window theme test failed: ${test_result}")
endif()
