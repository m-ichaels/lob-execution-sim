cmake_minimum_required(VERSION 3.20)
project(lob_execution_sim CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

if(MSVC)
  add_compile_options(/W3 /EHsc /permissive- /Zc:__cplusplus /utf-8)
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
else()
  add_compile_options(-Wall -Wextra)
endif()

add_library(lobsim INTERFACE)
target_include_directories(lobsim INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_executable(lobsim_cli src/main.cpp)
target_link_libraries(lobsim_cli PRIVATE lobsim)
set_target_properties(lobsim_cli PROPERTIES OUTPUT_NAME lobsim)

add_executable(fixgw src/fix_gateway.cpp)
target_link_libraries(fixgw PRIVATE lobsim)
if(WIN32)
  target_link_libraries(fixgw PRIVATE ws2_32)
endif()

add_executable(known_answer_tests tests/known_answer_tests.cpp)
target_link_libraries(known_answer_tests PRIVATE lobsim)
if(WIN32)
  target_link_libraries(known_answer_tests PRIVATE ws2_32)
endif()

enable_testing()
add_test(NAME known_answer_tests COMMAND known_answer_tests)
