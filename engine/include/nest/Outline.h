#pragma once
// The full geometry pipeline from section 4 of OUTLINE_NESTING_SPEC.md:
// flatten, union, discard holes, simplify, offset by half the gap.

#include "nest/Flatten.h"
#include "nest/Types.h"

namespace nest {

struct OutlineOptions {
  double flattenTolerance{kDefaultToleranceMm};
  double simplifyTolerance{kDefaultToleranceMm};
  // Minimum clear distance between two placed outlines. Each outline is grown
  // by half of it, so two grown outlines touching means the sources are `gap`
  // apart.
  double gap{0.0};
};

Polygons buildOutline(const std::vector<BezierPath>& paths,
                      const OutlineOptions& options = {});

// A cheaper stand-in for an outline, used only for collision during the
// search. The cost of a no-fit polygon rises roughly as the fourth power of
// the vertex count, so halving the vertices is worth more than any other
// optimisation available here.
//
// Simplifying alone would be unsafe: Douglas-Peucker cuts inside the shape by
// up to the tolerance, and a collision outline that is too small places pieces
// closer than the gap allows. Growing the simplified ring back by the same
// tolerance with miter joins guarantees it contains the original, so the
// search can only ever be too cautious. The exact ring is kept for the
// validation gate.
// `maxVertices` caps the cost. The tolerance alone cannot: it is an absolute
// distance, while what the search actually pays for is the vertex count, and
// the relationship between the two depends entirely on how curvy the artwork
// is. A real die-cut sticker offset by a 5 mm gap came out at 520 vertices,
// where one no-fit polygon took six seconds. Simplified to 85 it takes 219 ms.
//
// So the tolerance is loosened until the ring fits the cap. That costs yield,
// because the ring is grown by whatever tolerance is settled on, and the growth
// is reported so a caller can say what it cost. Zero means no cap.
Polygon searchOutline(const Polygon& ring,
                      double tolerance = kDefaultToleranceMm,
                      std::size_t maxVertices = 0,
                      double* toleranceUsed = nullptr);

}  // namespace nest
