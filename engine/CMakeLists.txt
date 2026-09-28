# The nesting engine. Pure C++20 plus Clipper2.
# Rule from TECH_STACK_DECISION.md: no Adobe header may ever appear here.
add_library(nest_engine STATIC
  src/geometry/Types.cpp
  src/geometry/Flatten.cpp
  src/geometry/Simplify.cpp
  src/geometry/Clipping.cpp
  src/geometry/Outline.cpp
  src/geometry/Transform.cpp
  src/nfp/Minkowski.cpp
  src/nfp/Convex.cpp
  src/nfp/NfpCache.cpp
  src/search/BottomLeftFill.cpp
  src/search/Refine.cpp
  src/search/GuidedSearch.cpp
  src/search/Lattice.cpp
  src/Svg.cpp
  src/Validate.cpp
)
add_library(nest::engine ALIAS nest_engine)

target_include_directories(nest_engine PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

find_package(Threads REQUIRED)
target_link_libraries(nest_engine PRIVATE Clipper2 Threads::Threads)

if(MSVC)
  target_compile_options(nest_engine PRIVATE /W4)
else()
  target_compile_options(nest_engine PRIVATE -Wall -Wextra -Wpedantic)
endif()
