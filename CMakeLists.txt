cmake_minimum_required(VERSION 3.28)
project(nesting_engine LANGUAGES CXX VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# The WebAssembly build of the engine, which is what the web app consumes. Off
# by default because it needs Emscripten and a plain native build of the
# library has no use for it.
option(NEST_BUILD_WASM "Build the WebAssembly module (needs emcmake)" OFF)

include(cmake/Dependencies.cmake)

add_subdirectory(engine)

if(NEST_BUILD_WASM)
  add_subdirectory(wasm)
endif()
