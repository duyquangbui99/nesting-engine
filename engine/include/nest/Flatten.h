#pragma once
// Step one of the outline pipeline: turn Bezier path data into polylines.

#include "nest/Types.h"

namespace nest {

// One cubic Bezier segment, described the way Illustrator describes them:
// two control points plus the on-curve end point. The start point comes from
// the previous segment, or from BezierPath::start for the first one.
struct CubicSegment {
  Point control1{};
  Point control2{};
  Point end{};
};

// A path as read from a document. A straight line is a segment whose control
// points sit on the chord.
struct BezierPath {
  Point start{};
  std::vector<CubicSegment> segments;
  bool closed{true};
};

// Appends the flattened points of one cubic to `out`, excluding p0 and
// including p3. Subdivides until the control points sit within `tolerance`
// of the chord.
void flattenCubic(const Point& p0, const Point& p1, const Point& p2,
                  const Point& p3, double tolerance, Polygon& out);

// Flattens a whole path into a ring. Duplicate consecutive points are dropped.
Polygon flatten(const BezierPath& path, double tolerance = kDefaultToleranceMm);
Polygons flatten(const std::vector<BezierPath>& paths,
                 double tolerance = kDefaultToleranceMm);

// Convenience builders for straight-edged shapes and for tests.
BezierPath makePolygonPath(const Polygon& ring);
BezierPath makeCirclePath(const Point& center, double radius);
BezierPath makeRectPath(const Rect& r);

}  // namespace nest
