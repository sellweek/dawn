cmake_minimum_required(VERSION 3.10)
project(dawn)

set(CMAKE_CXX_STANDARD 17)
find_package(LLVM REQUIRED CONFIG)

add_definitions(${LLVM_DEFINITIONS})
include_directories(${LLVM_INCLUDE_DIRS})
llvm_map_components_to_libnames(llvm_libs core)

add_executable(dawn main.cpp)

target_link_libraries(dawn ${llvm_libs})
