execute_process(COMMAND "${FIXTURE}" "${RUNTIME}" "${COUNT}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 25)
message("${output}${errors}")
if(NOT "${result}" STREQUAL "0" OR NOT output MATCHES "context calls completed")
    message(FATAL_ERROR "Context 测试未完成，退出结果：${result}")
endif()
