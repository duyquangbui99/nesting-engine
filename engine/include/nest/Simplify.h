#pragma once
// Douglas-Peucker simplification, ring aware.

#include "nest/Types.h"

namespace nest {

// Simplifies a closed ring. Rings of three points or fewer are returned as is.
Polygon simplify(const Polygon& ring, double tolerance = kDefaultToleranceMm);
Polygons simplify(const Polygons& rings, double tolerance = kDefaultToleranceMm);

}  // namespace nest
