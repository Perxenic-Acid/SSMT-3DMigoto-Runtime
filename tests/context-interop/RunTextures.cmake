execute_process(COMMAND "${FIXTURE}" "${RUNTIME}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 25)
message("${output}${errors}")
if(NOT "${result}" STREQUAL "0" OR NOT output MATCHES "texture replacement checks completed: PASS")
    message(FATAL_ERROR "纹理替换回读未完成，退出结果：${result}")
endif()
