cmake_minimum_required(VERSION 3.20)
project(llvm-jutsu C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(LLVM REQUIRED CONFIG)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")
message(STATUS "LLVM include dirs: ${LLVM_INCLUDE_DIRS}")

add_definitions(${LLVM_DEFINITIONS})

# Some LLVM cmake configs (e.g. Homebrew keg-only installs, LLVM 17+) leave
# LLVM_INCLUDE_DIRS empty and rely on imported targets instead.  Derive the
# include path from LLVM_INSTALL_PREFIX when available, falling back to
# computing it from LLVM_DIR (<prefix>/lib/cmake/llvm -> <prefix>).
if(NOT LLVM_INCLUDE_DIRS)
  if(DEFINED LLVM_INSTALL_PREFIX AND LLVM_INSTALL_PREFIX)
    set(LLVM_INCLUDE_DIRS "${LLVM_INSTALL_PREFIX}/include")
  elseif(DEFINED LLVM_DIR AND LLVM_DIR)
    # LLVM_DIR typically points to <prefix>/lib/cmake/llvm.
    # Walk up from the cmake config directory to find the install prefix.
    get_filename_component(_llvm_cmake_dir "${LLVM_DIR}" ABSOLUTE)
    get_filename_component(_llvm_cmake_parent "${_llvm_cmake_dir}" DIRECTORY)   # lib/cmake
    get_filename_component(_llvm_lib_dir "${_llvm_cmake_parent}" DIRECTORY)     # lib
    get_filename_component(_llvm_prefix "${_llvm_lib_dir}" DIRECTORY)           # install root
    set(LLVM_INCLUDE_DIRS "${_llvm_prefix}/include")
  endif()
  message(STATUS "Derived LLVM include dirs: ${LLVM_INCLUDE_DIRS}")
endif()

include_directories(${LLVM_INCLUDE_DIRS})

add_library(llvm-jutsu MODULE
  llvm-jutsu.cpp
  lodepng.cpp
)

set_target_properties(llvm-jutsu PROPERTIES PREFIX "")

if(APPLE)
  # On macOS, allow undefined symbols to be resolved at load time from the host
  # process (opt/clang which already links LLVM), and use .dylib extension so
  # the plugin can be loaded with -load-pass-plugin.
  target_link_options(llvm-jutsu PRIVATE -undefined dynamic_lookup)
  set_target_properties(llvm-jutsu PROPERTIES SUFFIX ".dylib")
endif()

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
