cmake_minimum_required(VERSION 3.20)
project(llvm-function-clone)

find_package(LLVM REQUIRED CONFIG)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

include_directories(${LLVM_INCLUDE_DIRS})
separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND ${LLVM_DEFINITIONS})
add_definitions(${LLVM_DEFINITIONS_LIST})

add_executable(llvm-function-clone main.cpp)

llvm_map_components_to_libnames(llvm_libs support core irreader transformutils mcjit mc nativecodegen executionengine interpreter x86asmparser)
target_link_libraries(llvm-function-clone ${llvm_libs})
target_compile_features(llvm-function-clone PRIVATE cxx_std_17)

configure_file(module-a.ll module-a.ll COPYONLY)
configure_file(module-b.ll module-b.ll COPYONLY)