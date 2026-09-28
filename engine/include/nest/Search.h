#pragma once
// Guided search. Section 5 of OUTLINE_NESTING_SPEC.md.
//
// A single bottom-left pass is fast and order-sensitive: the same pieces in a
// different order give a very different sheet, and giving a greedy pass finer
// rotations makes it worse rather than better. The search fixes that by running
// many passes with different orderings and rotation assignments and keeping the
// best, evolving the population rather than sampling it at random.
//
// The engine takes a wall-clock budget rather than an iteration count, so a
// weak machine returns a worse answer in the same time instead of the same
// answer in a longer time. Stopping early is always safe: the best layout found
// so far is complete and valid.

#include <atomic>
#include <cstdint>
#include <functional>

#include "nest/Lattice.h"
#include "nest/Nest.h"

namespace nest {

struct SearchOptions {
  // Wall clock, in seconds. The spec's presets are 2 s fast, 10 s balanced,
  // 60 s best.
  double timeBudgetSeconds{10.0};
  int populationSize{12};
  // Stops after this many generations regardless of the budget. Zero means the
  // budget alone decides. Tests use it to get a run that does not depend on how
  // fast the machine is.
  int maxGenerations{0};
  // Worker threads. Zero means one per core bar the caller's, clamped to
  // sixteen, which is the spec's rule.
  int threads{0};
  // Fixed by default: the same request yields the same sheet every time, on
  // any number of threads.
  std::uint32_t seed{1};
  // Called on the caller's thread each time the best layout improves, so a
  // preview can update while the search runs. Never called concurrently.
  std::function<void(const NestResult&)> onImprovement;
  // Try regular tilings before evolving anything. Cheap, and for a sheet of
  // identical stickers often better than the search reaches in seconds.
  bool useLattice{true};
  LatticeOptions lattice;
};

struct SearchResult {
  NestResult best;
  int generations{0};
  int evaluations{0};
  double elapsedSeconds{0.0};
  // When the first valid layout became available. The spec wants something on
  // screen inside about 200 ms for 500 pieces.
  double firstLayoutSeconds{0.0};
  bool cancelled{false};
  int threadsUsed{1};
  // True when the answer came from a tiling rather than from the population.
  bool bestFromLattice{false};
};

// `cancel` is polled between placements, so a stop is answered in about the
// time one piece takes to place.
SearchResult guidedSearch(const NestRequest& request,
                          const SearchOptions& options = {},
                          const std::atomic<bool>* cancel = nullptr);

}  // namespace nest
