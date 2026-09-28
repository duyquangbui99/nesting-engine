# The engine as a WebAssembly module.
#
# Built with emcmake, which is the only thing here that needs Emscripten:
#
#   emcmake cmake --preset wasm && cmake --build --preset wasm
#
# The output is committed into the web app, so nobody working on the web side
# needs a C++ toolchain at all. Only rebuilding the engine does.

if(NOT EMSCRIPTEN)
  message(FATAL_ERROR
    "The wasm target needs Emscripten. Configure with emcmake:\n"
    "  emcmake cmake --preset wasm")
endif()

add_executable(nest_wasm nest_wasm.cpp)
target_link_libraries(nest_wasm PRIVATE nest::engine nlohmann_json::nlohmann_json)
target_compile_features(nest_wasm PRIVATE cxx_std_20)

set_target_properties(nest_wasm PROPERTIES OUTPUT_NAME "nest-engine")

# MODULARIZE with EXPORT_ES6 gives an importable module rather than something
# that scribbles on globals, which is what Next.js wants. ALLOW_MEMORY_GROWTH
# because a sheet of five hundred stickers needs far more than the default
# sixteen megabytes and the exact amount is not knowable up front.
target_link_options(nest_wasm PRIVATE
  "-sMODULARIZE=1"
  "-sEXPORT_ES6=1"
  "-sENVIRONMENT=web,worker,node"
  "-sALLOW_MEMORY_GROWTH=1"
  # Emscripten defaults the stack to 64 KB, which is nothing. Douglas-Peucker
  # recurses once per point it keeps, so a 500 vertex outline from real artwork
  # goes hundreds of frames deep and runs off the end as "memory access out of
  # bounds", with no hint that the stack is what ran out.
  "-sSTACK_SIZE=8MB"
  "-sEXPORTED_FUNCTIONS=['_nest_run','_nest_free','_nest_version','_malloc','_free']"
  "-sEXPORTED_RUNTIME_METHODS=['ccall','cwrap','UTF8ToString','stringToNewUTF8']"
  "-sDISABLE_EXCEPTION_CATCHING=0"
  "-O3"
)
