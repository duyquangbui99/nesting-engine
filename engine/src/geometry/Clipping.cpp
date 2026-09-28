#include "nest/Clipping.h"

#include <algorithm>

#include "clipper2/clipper.h"

namespace nest {
namespace {

using Clipper2Lib::PathD;
using Clipper2Lib::PathsD;
using Clipper2Lib::PointD;

PathsD toClipper(const Polygons& rings) {
  PathsD out;
  out.reserve(rings.size());
  for (const Polygon& ring : rings) {
    PathD path;
    path.reserve(ring.size());
    for (const Point& p : ring) path.push_back(PointD(p.x, p.y));
    out.push_back(std::move(path));
  }
  return out;
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

Polygons unionAll(const Polygons& rings) {
  if (rings.empty()) return {};
  // Orientation is left as Clipper2 produced it. A reversed ring is a hole,
  // and callers that need holes gone say so by asking for outerBoundaries.
  return fromClipper(Clipper2Lib::Union(
      toClipper(rings), Clipper2Lib::FillRule::NonZero, kClipperPrecision));
}

Polygons intersect(const Polygons& subject, const Polygons& clip) {
  if (subject.empty() || clip.empty()) return {};
  return fromClipper(Clipper2Lib::Intersect(toClipper(subject), toClipper(clip),
                                            Clipper2Lib::FillRule::NonZero,
                                            kClipperPrecision));
}

Polygons difference(const Polygons& subject, const Polygons& clip) {
  if (subject.empty()) return {};
  if (clip.empty()) return subject;
  return fromClipper(Clipper2Lib::Difference(toClipper(subject), toClipper(clip),
                                             Clipper2Lib::FillRule::NonZero,
                                             kClipperPrecision));
}

Polygons discardHoles(const Polygons& rings) {
  if (rings.size() <= 1) return rings;
  Polygons kept;
  for (std::size_t i = 0; i < rings.size(); ++i) {
    bool nested = false;
    for (std::size_t j = 0; j < rings.size() && !nested; ++j) {
      if (i == j) continue;
      if (containsPoint(rings[j], rings[i].front())) nested = true;
    }
    if (!nested) kept.push_back(rings[i]);
  }
  return kept.empty() ? rings : kept;
}

Polygons outerBoundaries(const Polygons& rings) {
  Polygons out = discardHoles(unionAll(rings));
  makeCounterClockwise(out);
  return out;
}

Polygons offset(const Polygons& rings, double delta, JoinStyle join,
                double arcTolerance) {
  if (rings.empty() || delta == 0.0) return rings;
  const Clipper2Lib::JoinType jt = join == JoinStyle::Miter
                                       ? Clipper2Lib::JoinType::Miter
                                       : Clipper2Lib::JoinType::Round;
  // Orientation is left alone so that shrinking a region with holes keeps its
  // holes, the same contract unionAll follows.
  return fromClipper(Clipper2Lib::InflatePaths(
      toClipper(rings), delta, jt, Clipper2Lib::EndType::Polygon, 8.0,
      kClipperPrecision, arcTolerance));
}

double penetrationDepth(const Polygon& a, const Polygon& b) {
  const Polygons shared = intersect(Polygons{a}, Polygons{b});
  const double sharedArea = area(shared);
  if (sharedArea <= 0.0) return 0.0;
  const Rect box = bounds(shared);
  const double span = std::max(box.width(), box.height());
  return span > 0.0 ? sharedArea / span : 0.0;
}

bool overlaps(const Polygon& a, const Polygon& b, double tolerance) {
  return penetrationDepth(a, b) > tolerance;
}

}  // namespace nest
