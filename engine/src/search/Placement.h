#pragma once
// Engine-internal placement core, shared by the plain bottom-left pass and the
// guided search. Not part of the public headers.

#include <atomic>
#include <chrono>
#include <vector>

#include "nest/Lattice.h"
#include "nest/Nest.h"
#include "nest/NfpCache.h"

namespace nest::internal {

// One instance waiting to be placed: which design, which copy of it, and which
// entry of that design's rotation table to use. A rotation index of -1 means
// try every rotation and keep the best, which is what a plain bottom-left pass
// does; the search pins one instead, so a pass costs a fraction as much and the
// choice becomes something evolution can act on.
struct Gene {
  int designIndex{0};
  int instanceIndex{0};
  int rotationIndex{-1};
};

// Everything derived from a request that does not change while searching:
// the usable sheet, each design's allowed angles, and its search outline
// pre-rotated to each of them. Built once, then shared read-only by every
// worker thread.
struct NestContext {
  Rect usable;
  std::vector<std::vector<double>> angles;
  std::vector<std::vector<Polygon>> shapes;
  std::vector<double> designArea;
  bool valid{false};
};

NestContext buildContext(const NestRequest& request);

// Every design in turn, every copy of it, rotation left free.
std::vector<Gene> naturalSequence(const NestRequest& request);

// Places the sequence in the order given.
//
// `cancel` and `deadline` are both checked between placements and between the
// rotations tried for one placement. Between placements alone is not enough: a
// single piece free to take any of 36 angles has to build a no-fit polygon for
// each, and on real artwork that is seconds of work inside one iteration of the
// outer loop. A two second budget ran for thirty-three.
//
// A default-constructed deadline means no deadline.
NestResult placeSequence(const NestRequest& request, const NestContext& context,
                         const std::vector<Gene>& sequence, NfpCache& cache,
                         const std::atomic<bool>* cancel = nullptr,
                         std::chrono::steady_clock::time_point deadline = {});

// Tiling pass, sharing the caller's context and cache so the search does not
// pay for the same no-fit polygons twice.
NestResult latticeFill(const NestRequest& request, const NestContext& context,
                       NfpCache& cache, const LatticeOptions& options);

}  // namespace nest::internal
