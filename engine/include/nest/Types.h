#pragma once
// Core geometric value types. Units are millimetres throughout the engine.
// No Adobe headers are permitted anywhere under engine/.

#include <algorithm>
#include <cstddef>
#include <limits>
#include <vector>

namespace nest {

// Default Bezier flattening / simplification tolerance from the spec:
// 0.05 mm, which is 0.14 pt.
inline constexpr double kDefaultToleranceMm = 0.05;

// Decimal digits Clipper2 keeps when it converts our doubles to its own
// integer grid. Four digits is 0.0001 mm, far below kDefaultToleranceMm.
inline constexpr int kClipperPrecision = 4;

// Two pieces that touch exactly land on that 0.0001 mm grid, so a contact can
// read as a hair of overlap or a hair of clearance depending on rounding. One
// micron absorbs it. That is a fiftieth of the flattening tolerance and orders
// of magnitude below anything a cutter resolves.
inline constexpr double kPlacementToleranceMm = 1e-3;

// How far a region is pulled in before asking whether a point is inside it.
// Points that sit exactly on a boundary cannot be classified reliably, and a
// touching placement sits exactly there, so the region has to give a little.
//
// It must be far finer than the tolerance a layout is accepted at. Made equal,
// a layout can spend the whole allowance and land precisely on the threshold:
// a tiling of 20 mm squares with no gap came out pitched at 19.999 and failed
// validation by four ten-thousandths of a millimetre.
inline constexpr double kBoundaryEpsilonMm = kPlacementToleranceMm * 0.1;

struct Point {
  double x{0.0};
  double y{0.0};
};

inline bool operator==(const Point& a, const Point& b) {
  return a.x == b.x && a.y == b.y;
}

// A closed ring. The closing edge from back() to front() is implicit, so the
// first point is never repeated at the end.
using Polygon = std::vector<Point>;
using Polygons = std::vector<Polygon>;

struct Rect {
  double minX{std::numeric_limits<double>::max()};
  double minY{std::numeric_limits<double>::max()};
  double maxX{std::numeric_limits<double>::lowest()};
  double maxY{std::numeric_limits<double>::lowest()};

  bool valid() const { return maxX >= minX && maxY >= minY; }
  double width() const { return maxX - minX; }
  double height() const { return maxY - minY; }
  double area() const { return valid() ? width() * height() : 0.0; }

  void extend(const Point& p) {
    minX = std::min(minX, p.x);
    minY = std::min(minY, p.y);
    maxX = std::max(maxX, p.x);
    maxY = std::max(maxY, p.y);
  }
};

Rect bounds(const Polygon& ring);
Rect bounds(const Polygons& rings);

// Signed area by the shoelace formula. Positive means counter-clockwise.
double signedArea(const Polygon& ring);
double area(const Polygon& ring);
double area(const Polygons& rings);

bool containsPoint(const Polygon& ring, const Point& p);

// True when p lies in the region described by a set of rings, counting winding
// so that reversed rings read as holes rather than as solid islands.
bool containsPoint(const Polygons& rings, const Point& p);

// Rewrites each ring counter-clockwise so downstream code never has to guess.
void makeCounterClockwise(Polygon& ring);
void makeCounterClockwise(Polygons& rings);

}  // namespace nest
