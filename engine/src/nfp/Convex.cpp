#include "nest/Nfp.h"

namespace nest {

bool isConvex(const Polygon& ring) {
  if (ring.size() < 3) return false;
  const std::size_t n = ring.size();
  int sign = 0;
  for (std::size_t i = 0; i < n; ++i) {
    const Point& a = ring[i];
    const Point& b = ring[(i + 1) % n];
    const Point& c = ring[(i + 2) % n];
    const double cross =
        (b.x - a.x) * (c.y - b.y) - (b.y - a.y) * (c.x - b.x);
    if (cross == 0.0) continue;  // collinear vertices do not decide convexity
    const int s = cross > 0.0 ? 1 : -1;
    if (sign == 0) {
      sign = s;
    } else if (s != sign) {
      return false;
    }
  }
  return sign != 0;
}

}  // namespace nest
