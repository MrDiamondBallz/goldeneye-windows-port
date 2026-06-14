cmake_minimum_required(VERSION 3.20)
project(goldeneye_native_spike LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(N64RECOMP_DIR "/root/projects/goldeneye-pc-port/N64Recomp" CACHE PATH "Path to local N64Recomp checkout")
set(GE_GENERATED_DIR "${CMAKE_CURRENT_LIST_DIR}/../generated/us_recomp" CACHE PATH "Path to generated GoldenEye recomp output")

if(NOT EXISTS "${N64RECOMP_DIR}/include/recomp.h")
  message(FATAL_ERROR "Missing N64Recomp include/recomp.h. Set -DN64RECOMP_DIR=/path/to/N64Recomp")
endif()

if(NOT EXISTS "${GE_GENERATED_DIR}/funcs.h")
  message(FATAL_ERROR "Missing generated GoldenEye code at ${GE_GENERATED_DIR}. Run scripts/run_goldeneye_n64recomp_spike.sh first.")
endif()

file(GLOB GE_GENERATED_C CONFIGURE_DEPENDS "${GE_GENERATED_DIR}/funcs_*.c")
set(GE_GENERATED_CPP "${GE_GENERATED_DIR}/lookup.cpp")

add_library(goldeneye_recomp_generated STATIC
  ${GE_GENERATED_C}
  ${GE_GENERATED_CPP}
)

target_include_directories(goldeneye_recomp_generated PRIVATE
  "${CMAKE_CURRENT_LIST_DIR}/../runtime"
  "${GE_GENERATED_DIR}"
  "${N64RECOMP_DIR}/include"
)

target_compile_options(goldeneye_recomp_generated PRIVATE
  $<$<COMPILE_LANGUAGE:C>:-Wno-implicit-function-declaration -Wno-unused-variable -Wno-unused-but-set-variable -Wno-unused-label -Wno-attributes>
  $<$<COMPILE_LANGUAGE:CXX>:-Wno-unused-variable -Wno-unused-but-set-variable>
)

target_compile_definitions(goldeneye_recomp_generated PRIVATE
  N64RECOMP_RECOMP_H="${N64RECOMP_DIR}/include/recomp.h"
)

add_library(goldeneye_runtime STATIC
  "${CMAKE_CURRENT_LIST_DIR}/../runtime/runtime.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/../runtime/stubs.cpp"
)

target_include_directories(goldeneye_runtime PUBLIC
  "${CMAKE_CURRENT_LIST_DIR}/../runtime"
  "${GE_GENERATED_DIR}"
  "${N64RECOMP_DIR}/include"
)

target_compile_definitions(goldeneye_runtime PUBLIC
  N64RECOMP_RECOMP_H="${N64RECOMP_DIR}/include/recomp.h"
)

add_executable(goldeneye_native_spike
  "${CMAKE_CURRENT_LIST_DIR}/main.cpp"
)

target_include_directories(goldeneye_native_spike PRIVATE
  "${CMAKE_CURRENT_LIST_DIR}/../runtime"
  "${GE_GENERATED_DIR}"
  "${N64RECOMP_DIR}/include"
)

target_link_libraries(goldeneye_native_spike PRIVATE
  goldeneye_runtime
  goldeneye_recomp_generated
  m
)

target_compile_definitions(goldeneye_native_spike PRIVATE
  N64RECOMP_RECOMP_H="${N64RECOMP_DIR}/include/recomp.h"
)
