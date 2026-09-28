#include "nest/Flatten.h"

#include <cmath>

namespace nest {
namespace {

constexpr int kMaxSubdivisionDepth = 24;

// Distance from p to the infinite line through a and b. Falls back to the
// point distance when the chord is degenerate, which forces subdivision.
double distanceToChord(const Point& p, const Point& a, const Point& b) {
  const double dx = b.x - a.x;
  const double dy = b.y - a.y;
  const double len2 = dx * dx + dy * dy;
  if (len2 <= 0.0) return std::hypot(p.x - a.x, p.y - a.y);
  const double cross = std::abs(dx * (a.y - p.y) - dy * (a.x - p.x));
  return cross / std::sqrt(len2);
}

Point midpoint(const Point& a, const Point& b) {
  return Point{(a.x + b.x) * 0.5, (a.y + b.y) * 0.5};
}

void subdivide(const Point& p0, const Point& p1, const Point& p2,
               const Point& p3, double tolerance, int depth, Polygon& out) {
  // A cubic never strays further from its chord than the larger of the two
  // control point offsets, so this test is conservative.
  const double d1 = distanceToChord(p1, p0, p3);
  const double d2 = distanceToChord(p2, p0, p3);
  if (depth >= kMaxSubdivisionDepth || std::max(d1, d2) <= tolerance) {
    out.push_back(p3);
    return;
  }
  // de Casteljau split at t = 0.5.
  const Point p01 = midpoint(p0, p1);
  const Point p12 = midpoint(p1, p2);
  const Point p23 = midpoint(p2, p3);
  const Point p012 = midpoint(p01, p12);
  const Point p123 = midpoint(p12, p23);
  const Point mid = midpoint(p012, p123);
  subdivide(p0, p01, p012, mid, tolerance, depth + 1, out);
  subdivide(mid, p123, p23, p3, tolerance, depth + 1, out);
}

bool nearlyEqual(const Point& a, const Point& b, double eps) {
  return std::abs(a.x - b.x) <= eps && std::abs(a.y - b.y) <= eps;
}

}  // namespace

void flattenCubic(const Point& p0, const Point& p1, const Point& p2,
                  const Point& p3, double tolerance, Polygon& out) {
  subdivide(p0, p1, p2, p3, tolerance, 0, out);
}

Polygon flatten(const BezierPath& path, double tolerance) {
  Polygon out;
  if (path.segments.empty()) return out;
  out.push_back(path.start);
  Point current = path.start;
  for (const CubicSegment& seg : path.segments) {
    flattenCubic(current, seg.control1, seg.control2, seg.end, tolerance, out);
    current = seg.end;
  }

  // Drop duplicate neighbours, then the repeated closing point if present.
  const double eps = tolerance * 1e-3;
  Polygon cleaned;
  cleaned.reserve(out.size());
  for (const Point& p : out)
    if (cleaned.empty() || !nearlyEqual(cleaned.back(), p, eps))
      cleaned.push_back(p);
  while (cleaned.size() > 1 && nearlyEqual(cleaned.front(), cleaned.back(), eps))
    cleaned.pop_back();
  return cleaned;
}

Polygons flatten(const std::vector<BezierPath>& paths, double tolerance) {
  Polygons out;
  out.reserve(paths.size());
  for (const BezierPath& path : paths) {
    Polygon ring = flatten(path, tolerance);
    if (ring.size() >= 3) out.push_back(std::move(ring));
  }
  return out;
}

BezierPath makePolygonPath(const Polygon& ring) {
  BezierPath path;
  if (ring.empty()) return path;
  path.start = ring.front();
  path.closed = true;
  Point prev = ring.front();
  for (std::size_t i = 1; i <= ring.size(); ++i) {
    const Point& next = ring[i % ring.size()];
    // Control points on the chord make this a straight line.
    path.segments.push_back(CubicSegment{
        Point{prev.x + (next.x - prev.x) / 3.0, prev.y + (next.y - prev.y) / 3.0},
        Point{prev.x + 2.0 * (next.x - prev.x) / 3.0,
              prev.y + 2.0 * (next.y - prev.y) / 3.0},
        next});
    prev = next;
  }
  return path;
}

BezierPath makeCirclePath(const Point& center, double radius) {
  // Four cubics, the standard circle approximation.
  constexpr double k = 0.5522847498307933;
  const double r = radius;
  const double kr = k * r;
  const double cx = center.x;
  const double cy = center.y;

  BezierPath path;
  path.closed = true;
  path.start = Point{cx + r, cy};
  path.segments = {
      CubicSegment{{cx + r, cy + kr}, {cx + kr, cy + r}, {cx, cy + r}},
      CubicSegment{{cx - kr, cy + r}, {cx - r, cy + kr}, {cx - r, cy}},
      CubicSegment{{cx - r, cy - kr}, {cx - kr, cy - r}, {cx, cy - r}},
      CubicSegment{{cx + kr, cy - r}, {cx + r, cy - kr}, {cx + r, cy}},
  };
  return path;
}

BezierPath makeRectPath(const Rect& r) {
  return makePolygonPath(Polygon{{r.minX, r.minY},
                                 {r.maxX, r.minY},
                                 {r.maxX, r.maxY},
                                 {r.minX, r.maxY}});
}

}  // namespace nest
