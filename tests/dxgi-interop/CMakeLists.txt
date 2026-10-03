cmake_minimum_required(VERSION 3.20)
project(dxgi_interop_test LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "此测试需要 Windows 和可用的桌面输出。")
endif()

add_executable(interop-test main.cpp)
target_compile_features(interop-test PRIVATE cxx_std_17)
target_link_libraries(interop-test PRIVATE dbghelp)
if(MSVC)
    target_compile_options(interop-test PRIVATE /utf-8)
endif()
