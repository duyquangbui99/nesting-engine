#include "nest/Simplify.h"

#include <cmath>

namespace nest {
namespace {

double distanceToSegment(const Point& p, const Point& a, const Point& b) {
  const double dx = b.x - a.x;
  const double dy = b.y - a.y;
  const double len2 = dx * dx + dy * dy;
  if (len2 <= 0.0) return std::hypot(p.x - a.x, p.y - a.y);
  double t = ((p.x - a.x) * dx + (p.y - a.y) * dy) / len2;
  t = std::clamp(t, 0.0, 1.0);
  return std::hypot(p.x - (a.x + t * dx), p.y - (a.y + t * dy));
}

void douglasPeucker(const Polygon& pts, std::size_t first, std::size_t last,
                    double tolerance, std::vector<bool>& keep) {
  if (last <= first + 1) return;
  double worst = -1.0;
  std::size_t worstIndex = first;
  for (std::size_t i = first + 1; i < last; ++i) {
    const double d = distanceToSegment(pts[i], pts[first], pts[last]);
    if (d > worst) {
      worst = d;
      worstIndex = i;
    }
  }
  if (worst <= tolerance) return;
  keep[worstIndex] = true;
  douglasPeucker(pts, first, worstIndex, tolerance, keep);
  douglasPeucker(pts, worstIndex, last, tolerance, keep);
}

}  // namespace

Polygon simplify(const Polygon& ring, double tolerance) {
  if (ring.size() <= 3 || tolerance <= 0.0) return ring;

  // A ring has no natural endpoints. Anchor it at the start point and at the
  // vertex furthest from it, then run Douglas-Peucker on the two chains.
  Polygon pts = ring;
  pts.push_back(ring.front());
  const std::size_t n = pts.size();

  std::size_t anchor = 1;
  double best = -1.0;
  for (std::size_t i = 1; i + 1 < n; ++i) {
    const double d = std::hypot(pts[i].x - pts[0].x, pts[i].y - pts[0].y);
    if (d > best) {
      best = d;
      anchor = i;
    }
  }

  std::vector<bool> keep(n, false);
  keep[0] = true;
  keep[anchor] = true;
  keep[n - 1] = true;
  douglasPeucker(pts, 0, anchor, tolerance, keep);
  douglasPeucker(pts, anchor, n - 1, tolerance, keep);

  Polygon out;
  out.reserve(n);
  for (std::size_t i = 0; i + 1 < n; ++i)
    if (keep[i]) out.push_back(pts[i]);
  return out.size() >= 3 ? out : ring;
}

Polygons simplify(const Polygons& rings, double tolerance) {
  Polygons out;
  out.reserve(rings.size());
  for (const Polygon& ring : rings) out.push_back(simplify(ring, tolerance));
  return out;
}

}  // namespace nest
