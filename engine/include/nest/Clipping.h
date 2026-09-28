#pragma once
// Thin wrappers over Clipper2 so the rest of the engine never includes it.

#include "nest/Types.h"

namespace nest {

Polygons unionAll(const Polygons& rings);
Polygons intersect(const Polygons& subject, const Polygons& clip);
Polygons difference(const Polygons& subject, const Polygons& clip);

// Drops every ring that lies inside another ring.
Polygons discardHoles(const Polygons& rings);

// Union, then drop every ring that lies inside another ring. Stickers are
// solid, so interior holes never take part in collision.
Polygons outerBoundaries(const Polygons& rings);

// How a corner is carried across an outward offset. Round follows the true
// arc and adds vertices; Miter extends the two edges to their crossing, adds
// none, and always covers what Round would.
enum class JoinStyle { Round, Miter };

// Grows rings outward by `delta`. A negative delta shrinks. `arcTolerance`
// bounds how far the segments approximating a round join fall inside the true
// arc, and is ignored for miter joins.
Polygons offset(const Polygons& rings, double delta,
                JoinStyle join = JoinStyle::Round,
                double arcTolerance = kDefaultToleranceMm * 0.25);

// Mean depth by which two rings interpenetrate: the area they share divided by
// the longer side of that area's bounding box. Zero when they are disjoint.
//
// Depth rather than area is what matters. Two pieces touching along a 40 mm
// edge and two touching at a corner both round onto Clipper2's 0.0001 mm grid,
// but the shared area differs by orders of magnitude between them. Depth does
// not, which makes one threshold work for every contact length.
double penetrationDepth(const Polygon& a, const Polygon& b);

// True when two rings interpenetrate by more than `tolerance`. Pieces placed
// exactly touching read as a sliver a fraction of a micron deep, which this
// ignores; anything a cutter could see, it does not.
bool overlaps(const Polygon& a, const Polygon& b,
              double tolerance = kPlacementToleranceMm);

}  // namespace nest
