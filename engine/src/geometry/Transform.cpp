#include "nest/Transform.h"

#include <cmath>
#include <numbers>

namespace nest {

Point rotate(const Point& p, double degrees) {
  const double r = degrees * std::numbers::pi / 180.0;
  const double c = std::cos(r);
  const double s = std::sin(r);
  return Point{p.x * c - p.y * s, p.x * s + p.y * c};
}

Polygon rotated(const Polygon& ring, double degrees) {
  if (degrees == 0.0) return ring;
  Polygon out;
  out.reserve(ring.size());
  for (const Point& p : ring) out.push_back(rotate(p, degrees));
  return out;
}

Polygons rotated(const Polygons& rings, double degrees) {
  Polygons out;
  out.reserve(rings.size());
  for (const Polygon& ring : rings) out.push_back(rotated(ring, degrees));
  return out;
}

Polygon translated(const Polygon& ring, const Point& by) {
  Polygon out;
  out.reserve(ring.size());
  for (const Point& p : ring) out.push_back(Point{p.x + by.x, p.y + by.y});
  return out;
}

Polygons translated(const Polygons& rings, const Point& by) {
  Polygons out;
  out.reserve(rings.size());
  for (const Polygon& ring : rings) out.push_back(translated(ring, by));
  return out;
}

Polygon negated(const Polygon& ring) {
  Polygon out;
  out.reserve(ring.size());
  for (auto it = ring.rbegin(); it != ring.rend(); ++it)
    out.push_back(Point{-it->x, -it->y});
  return out;
}

Polygons negated(const Polygons& rings) {
  Polygons out;
  out.reserve(rings.size());
  for (const Polygon& ring : rings) out.push_back(negated(ring));
  return out;
}

}  // namespace nest
