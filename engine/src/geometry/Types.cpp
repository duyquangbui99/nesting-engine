#include "nest/Types.h"

#include <cmath>

namespace nest {

Rect bounds(const Polygon& ring) {
  Rect r;
  for (const Point& p : ring) r.extend(p);
  return r;
}

Rect bounds(const Polygons& rings) {
  Rect r;
  for (const Polygon& ring : rings)
    for (const Point& p : ring) r.extend(p);
  return r;
}

double signedArea(const Polygon& ring) {
  if (ring.size() < 3) return 0.0;
  double sum = 0.0;
  for (std::size_t i = 0, n = ring.size(); i < n; ++i) {
    const Point& a = ring[i];
    const Point& b = ring[(i + 1) % n];
    sum += a.x * b.y - b.x * a.y;
  }
  return sum * 0.5;
}

double area(const Polygon& ring) { return std::abs(signedArea(ring)); }

double area(const Polygons& rings) {
  double total = 0.0;
  for (const Polygon& ring : rings) total += area(ring);
  return total;
}

bool containsPoint(const Polygon& ring, const Point& p) {
  // Crossing number. Points exactly on an edge are not reliably classified,
  // which is fine for every caller here.
  bool inside = false;
  for (std::size_t i = 0, n = ring.size(), j = n - 1; i < n; j = i++) {
    const Point& a = ring[i];
    const Point& b = ring[j];
    if ((a.y > p.y) != (b.y > p.y)) {
      const double t = (p.y - a.y) / (b.y - a.y);
      if (p.x < a.x + t * (b.x - a.x)) inside = !inside;
    }
  }
  return inside;
}

bool containsPoint(const Polygons& rings, const Point& p) {
  int winding = 0;
  for (const Polygon& ring : rings)
    if (containsPoint(ring, p)) winding += signedArea(ring) > 0.0 ? 1 : -1;
  return winding != 0;
}

void makeCounterClockwise(Polygon& ring) {
  if (signedArea(ring) < 0.0) std::reverse(ring.begin(), ring.end());
}

void makeCounterClockwise(Polygons& rings) {
  for (Polygon& ring : rings) makeCounterClockwise(ring);
}

}  // namespace nest
