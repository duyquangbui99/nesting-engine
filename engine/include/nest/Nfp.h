#pragma once
// No-Fit Polygon. Section 5 of OUTLINE_NESTING_SPEC.md.
//
// NFP(A, B) is the set of translations of B at which B overlaps A. Place B's
// reference point strictly outside the NFP and the two pieces cannot collide;
// place it on the boundary and they touch exactly. Every collision question in
// the nester reduces to a point-in-polygon test against this shape, which is
// why it is worth precomputing and caching.

#include "nest/Types.h"

namespace nest {

// Minkowski sum of two rings, A + B.
Polygons minkowskiSum(const Polygon& a, const Polygon& b);

// NFP of a stationary piece and an orbiting one. Both rings are taken in their
// own coordinates; the result is in the orbiting piece's translation space.
Polygons noFitPolygon(const Polygon& stationary, const Polygon& orbiting);

// Inner-Fit Polygon for a rectangular sheet: the translations of `orbiting`
// that keep it wholly inside `sheet`. The sheet is always a rectangle here, so
// this is exact and costs nothing. Returns an empty ring when the piece does
// not fit at all.
Polygon innerFitRectangle(const Rect& sheet, const Polygon& orbiting);

bool isConvex(const Polygon& ring);

}  // namespace nest
