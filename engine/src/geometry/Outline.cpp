#include "nest/Outline.h"

#include <algorithm>

#include "nest/Clipping.h"
#include "nest/Simplify.h"

namespace nest {

Polygons buildOutline(const std::vector<BezierPath>& paths,
                      const OutlineOptions& options) {
  Polygons rings = flatten(paths, options.flattenTolerance);
  if (rings.empty()) return {};

  rings = outerBoundaries(rings);
  rings = simplify(rings, options.simplifyTolerance);

  // Deliberately no simplify after the offset. Every simplification pass cuts
  // slightly inside the shape, and cutting inside the gap offset would eat the
  // clearance the gap exists to guarantee. Vertex count here is controlled by
  // the offset's arc tolerance instead.
  if (options.gap > 0.0) rings = offset(rings, options.gap * 0.5);
  makeCounterClockwise(rings);
  return rings;
}

namespace {

// One simplify-then-grow pass. Returns the ring unchanged when simplifying at
// this tolerance achieves nothing.
Polygon leanAt(const Polygon& ring, double tolerance) {
  const Polygon lean = simplify(ring, tolerance);
  if (lean.size() >= ring.size()) return ring;

  Polygons grown = offset({lean}, tolerance, JoinStyle::Miter);
  grown = discardHoles(grown);
  if (grown.empty()) return ring;

  // An offset can split a thin waist into pieces; the largest is the body.
  std::size_t biggest = 0;
  for (std::size_t i = 1; i < grown.size(); ++i)
    if (area(grown[i]) > area(grown[biggest])) biggest = i;

  Polygon out = grown[biggest];
  makeCounterClockwise(out);
  return out.size() < ring.size() ? out : ring;
}

}  // namespace

Polygon searchOutline(const Polygon& ring, double tolerance,
                      std::size_t maxVertices, double* toleranceUsed) {
  if (toleranceUsed) *toleranceUsed = tolerance;
  if (tolerance <= 0.0 || ring.size() < 4) return ring;

  Polygon best = leanAt(ring, tolerance);
  if (maxVertices == 0 || best.size() <= maxVertices) return best;

  // Loosen until it fits. Growing by 1.6 each time reaches any sane cap in a
  // handful of steps, and stopping at a quarter of the shape's smaller side
  // keeps a runaway from simplifying a sticker into a triangle.
  const Rect box = bounds(ring);
  const double ceiling = std::max(tolerance, std::min(box.width(), box.height()) * 0.25);

  double attempt = tolerance;
  while (best.size() > maxVertices && attempt < ceiling) {
    attempt = std::min(attempt * 1.6, ceiling);
    Polygon leaner = leanAt(ring, attempt);
    if (leaner.size() >= best.size()) break;  // no further gain to be had
    best = std::move(leaner);
    if (toleranceUsed) *toleranceUsed = attempt;
  }
  return best;
}

}  // namespace nest
