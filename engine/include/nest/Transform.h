#pragma once
// Rigid transforms. Rotation is about the origin; callers that want to rotate
// about a centre translate first.

#include "nest/Types.h"

namespace nest {

Point rotate(const Point& p, double degrees);
Polygon rotated(const Polygon& ring, double degrees);
Polygons rotated(const Polygons& rings, double degrees);

Polygon translated(const Polygon& ring, const Point& by);
Polygons translated(const Polygons& rings, const Point& by);

// Reflects through the origin, which is the -B in NFP(A, B) = A + (-B).
// Reflection flips winding, so the vertex order is reversed to keep rings
// counter-clockwise.
Polygon negated(const Polygon& ring);
Polygons negated(const Polygons& rings);

}  // namespace nest
