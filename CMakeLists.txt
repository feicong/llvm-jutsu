cmake_minimum_required(VERSION 3.20)
project(llvm-jutsu CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(LLVM REQUIRED CONFIG)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

add_definitions(${LLVM_DEFINITIONS})
include_directories(${LLVM_INCLUDE_DIRS})

add_library(llvm-jutsu MODULE
  llvm-jutsu.cpp
  lodepng.cpp
)

set_target_properties(llvm-jutsu PROPERTIES PREFIX "")

if(WIN32)
  # On Windows, DLLs cannot have unresolved symbols, so link against LLVM libs.
  if(TARGET LLVM)
    target_link_libraries(llvm-jutsu PRIVATE LLVM)
  else()
    llvm_map_components_to_libnames(llvm_libs
      Core
      Support
      Passes
      Analysis
      TransformUtils
    )
    target_link_libraries(llvm-jutsu PRIVATE ${llvm_libs})
  endif()
endif()
