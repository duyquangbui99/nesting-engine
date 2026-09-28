#include <cmath>

#include "clipper2/clipper.h"
#include "nest/Clipping.h"
#include "nest/Nfp.h"
#include "nest/Transform.h"

namespace nest {
namespace {

using Clipper2Lib::PathD;
using Clipper2Lib::PathsD;
using Clipper2Lib::PointD;

PathD toClipper(const Polygon& ring) {
  PathD path;
  path.reserve(ring.size());
  for (const Point& p : ring) path.push_back(PointD(p.x, p.y));
  return path;
}

Polygons fromClipper(const PathsD& paths) {
  Polygons out;
  out.reserve(paths.size());
  for (const PathD& path : paths) {
    if (path.size() < 3) continue;
    Polygon ring;
    ring.reserve(path.size());
    for (const PointD& p : path) ring.push_back(Point{p.x, p.y});
    out.push_back(std::move(ring));
  }
  return out;
}

}  // namespace

Polygons minkowskiSum(const Polygon& a, const Polygon& b) {
  if (a.size() < 3 || b.size() < 3) return {};
  // Clipper2 builds the sum from edge-pair quads, which cover only the band
  // along the boundary. The outer ring it returns is correct, but the interior
  // comes back as a hole, so the holes are dropped to get the solid region.
  //
  // A genuine hole is also possible when both pieces are concave: it marks a
  // pocket of A that B fits inside without touching. Dropping those too is
  // deliberate and safe. Section 10 of the spec puts nesting inside holes out
  // of scope for v1, and refusing a pocket can only cost yield, never produce
  // an overlap.
  Polygons sum = fromClipper(Clipper2Lib::MinkowskiSum(
      toClipper(a), toClipper(b), true, kClipperPrecision));
  sum = discardHoles(sum);
  makeCounterClockwise(sum);
  return sum;
}

Polygons noFitPolygon(const Polygon& stationary, const Polygon& orbiting) {
  // NFP(A, B) = A + (-B).
  return minkowskiSum(stationary, negated(orbiting));
}

Polygon innerFitRectangle(const Rect& sheet, const Polygon& orbiting) {
  if (orbiting.size() < 3 || !sheet.valid()) return {};
  const Rect b = bounds(orbiting);

  // Translating the piece by t puts its bounding box at [minX+t, maxX+t]. That
  // stays inside the sheet exactly when t is in the range below.
  const Rect fit{sheet.minX - b.minX, sheet.minY - b.minY, sheet.maxX - b.maxX,
                 sheet.maxY - b.maxY};
  if (fit.maxX < fit.minX || fit.maxY < fit.minY) return {};

  return Polygon{{fit.minX, fit.minY},
                 {fit.maxX, fit.minY},
                 {fit.maxX, fit.maxY},
                 {fit.minX, fit.maxY}};
}

}  // namespace nest
