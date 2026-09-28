include(FetchContent)

set(FETCHCONTENT_QUIET OFF)

# Clipper2 (Boost Software License 1.0) -- offset, union, difference,
# intersection, Minkowski sum. The CMake project lives in the CPP/ subdir.
FetchContent_Declare(
  clipper2
  GIT_REPOSITORY https://github.com/AngusJohnson/Clipper2.git
  GIT_TAG        Clipper2_1.5.4
  GIT_SHALLOW    TRUE
  SOURCE_SUBDIR  CPP
)
set(CLIPPER2_UTILS   OFF CACHE BOOL "" FORCE)
set(CLIPPER2_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CLIPPER2_TESTS   OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(clipper2)

if(NEST_BUILD_WASM)
  # Header only, MIT. Only the JSON boundary in wasm/ needs it; the engine
  # itself has no serialisation in it.
  FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    TRUE
  )
  set(JSON_BuildTests OFF CACHE INTERNAL "")
  FetchContent_MakeAvailable(nlohmann_json)
endif()
