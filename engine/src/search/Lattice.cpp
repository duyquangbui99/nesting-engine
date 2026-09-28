#include "nest/Lattice.h"

#include <algorithm>
#include <cmath>

#include "nest/Clipping.h"
#include "nest/Nfp.h"
#include "nest/Transform.h"

#include "Placement.h"

namespace nest {
namespace {

using internal::NestContext;

// The furthest neighbour, in rows or columns, that a tiling will be checked
// against.
//
// Four was the old value, and the reasoning behind it was wrong: it assumed
// the pitch always exceeds the no-fit polygon's own height. The bisection
// below drives the pitch down as far as the check allows, so the reach needed
// to prove a tiling clear grows without bound as the spacing tightens. A long
// concave piece interlocks to a pitch well under a quarter of its own height,
// and every row past the fourth went unexamined and unreported.
//
// A tiling that needs more reach than this is now called unclear rather than
// assumed clear. The bisection then settles on a wider pitch, which costs a
// little yield and cannot produce an overlap.
constexpr int kMaxNeighbourSteps = 64;

// A no-fit polygon prepared for repeated point tests: pulled in a hair so that
// pieces touching exactly read as clear, with its bounding box kept alongside
// to reject distant points without walking it.
//
// The amount matters. Whatever it is, the spacing this finds is short by
// exactly that much, because the pitch is bisected against this shrunk region.
// Pulling in by the placement tolerance therefore spends the entire overlap
// allowance before a single piece is placed.
struct Blocker {
  Polygons interior;
  Rect box;

  bool blocks(const Point& p) const {
    if (p.x < box.minX || p.x > box.maxX || p.y < box.minY || p.y > box.maxY)
      return false;
    return containsPoint(interior, p);
  }
};

Blocker makeBlocker(const Polygons& nfp) {
  Blocker blocker;
  blocker.interior = offset(nfp, -kBoundaryEpsilonMm);
  blocker.box = bounds(nfp);
  return blocker;
}

// Rows alternate between two rotations and two horizontal offsets, so the
// layout repeats every two rows. Everything needed to test a spacing lives
// here.
struct Tiling {
  int rotationIndex{0};
  int alternateIndex{0};
  double pitchX{0.0};
  double pitchY{0.0};
  double stagger{0.0};
  // Where each row type starts: the corner of its own inner-fit rectangle.
  // The two differ whenever the alternate rotation changes the shape's
  // bounding box, which is to say for every piece that is not symmetric about
  // its own centre. Both the clearance check and the layout read the spacing
  // from here, because when they disagreed about it the check was proving one
  // tiling clear and the layout was drawing another.
  Rect fit[2]{};
};

// Four blockers, one per ordered pair of the two row types.
struct Blockers {
  Blocker pair[2][2];
  double maxWidth{0.0};
  double maxHeight{0.0};
};

// True when no two pieces of the tiling overlap.
//
// Checking only the neighbour to the right and the one above is not enough and
// was the bug in the first version of this: with alternating rows the piece
// diagonally above overlaps long before either of those do. Every neighbour
// within reach of the no-fit polygon has to be cleared.
bool tilingIsClear(const Tiling& tiling, const Blockers& blockers) {
  if (!(tiling.pitchX > 0.0) || !(tiling.pitchY > 0.0)) return false;

  // Written so that a pitch small enough to make these infinite, or NaN, fails
  // the comparison and reports the tiling unclear.
  const double rowsNeeded = std::ceil(blockers.maxHeight / tiling.pitchY) + 1.0;
  const double columnsNeeded = std::ceil(blockers.maxWidth / tiling.pitchX) + 1.0;
  if (!(rowsNeeded <= kMaxNeighbourSteps) || !(columnsNeeded <= kMaxNeighbourSteps))
    return false;
  const int rowReach = static_cast<int>(rowsNeeded);
  const int columnReach = static_cast<int>(columnsNeeded);

  for (int parity = 0; parity < 2; ++parity) {
    const Rect& fromFit = tiling.fit[parity];
    const double fromX = fromFit.minX + (parity ? tiling.stagger : 0.0);
    for (int dRow = -rowReach; dRow <= rowReach; ++dRow) {
      const int otherParity = ((parity + dRow) % 2 + 2) % 2;
      const Rect& toFit = tiling.fit[otherParity];
      const double toX = toFit.minX + (otherParity ? tiling.stagger : 0.0);
      const Blocker& blocker = blockers.pair[parity][otherParity];

      for (int dColumn = -columnReach; dColumn <= columnReach; ++dColumn) {
        if (dRow == 0 && dColumn == 0) continue;
        // Exactly the translation between the two placements layoutTiling
        // makes, row origins included.
        const Point delta{toX - fromX + dColumn * tiling.pitchX,
                          toFit.minY - fromFit.minY + dRow * tiling.pitchY};
        if (blocker.blocks(delta)) return false;
      }
    }
  }
  return true;
}

// Smallest row pitch that clears every neighbour. Raising the pitch only ever
// moves rows further apart, so the condition is monotone and bisects cleanly.
double tightestPitchY(Tiling tiling, const Blockers& blockers) {
  double hi = blockers.maxHeight + 1.0;
  tiling.pitchY = hi;
  if (!tilingIsClear(tiling, blockers)) return 0.0;

  double lo = 0.0;
  for (int i = 0; i < 40; ++i) {
    const double mid = (lo + hi) * 0.5;
    tiling.pitchY = mid;
    if (tilingIsClear(tiling, blockers))
      hi = mid;
    else
      lo = mid;
  }
  return hi;
}

NestResult layoutTiling(const NestRequest& request, const NestContext& context,
                        const Tiling& tiling, int quantity) {
  NestResult result;
  const std::vector<Polygon>& shapes = context.shapes[0];
  const Design& design = request.designs[0];

  int placed = 0;
  for (int row = 0; placed < quantity; ++row) {
    const bool odd = (row % 2) != 0;
    const int rotationIndex = odd ? tiling.alternateIndex : tiling.rotationIndex;
    // The same rectangle the clearance check was proved against.
    const Rect& fit = tiling.fit[odd ? 1 : 0];
    if (!fit.valid()) break;

    const double y = fit.minY + row * tiling.pitchY;
    if (y > fit.maxY + kPlacementToleranceMm) break;

    const double startX = fit.minX + (odd ? tiling.stagger : 0.0);
    for (double x = startX;
         x <= fit.maxX + kPlacementToleranceMm && placed < quantity;
         x += tiling.pitchX) {
      Placement placement;
      placement.designIndex = 0;
      placement.instanceIndex = placed;
      placement.sheetIndex = 0;
      placement.x = x;
      placement.y = y;
      placement.rotationDeg =
          context.angles[0][static_cast<std::size_t>(rotationIndex)];
      result.placements.push_back(placement);

      for (const Point& p :
           translated(rotated(design.outline, placement.rotationDeg), Point{x, y}))
        result.occupied.extend(p);
      ++placed;
    }
  }

  result.unplaced = quantity - placed;
  result.sheetCount = placed > 0 ? 1 : 0;
  const double usableArea = context.usable.area();
  result.utilization =
      usableArea > 0.0 ? placed * context.designArea[0] / usableArea : 0.0;
  return result;
}

bool betterLayout(const NestResult& candidate, const NestResult& incumbent) {
  if (candidate.placements.empty()) return false;
  if (incumbent.placements.empty()) return true;
  if (candidate.unplaced != incumbent.unplaced)
    return candidate.unplaced < incumbent.unplaced;
  return candidate.occupied.area() < incumbent.occupied.area();
}

// Angles spread evenly across the rotation table, always including the first.
std::vector<int> sampleRotations(std::size_t available, int wanted) {
  std::vector<int> picked;
  if (available == 0) return picked;
  const std::size_t take =
      wanted <= 0 ? available
                  : std::min(available, static_cast<std::size_t>(wanted));
  for (std::size_t i = 0; i < take; ++i)
    picked.push_back(static_cast<int>(i * available / take));
  return picked;
}

int rotationOffsetBy(const std::vector<double>& angles, int from, double by) {
  const double wanted =
      std::fmod(angles[static_cast<std::size_t>(from)] + by, 360.0);
  for (std::size_t i = 0; i < angles.size(); ++i)
    if (std::abs(angles[i] - wanted) < 1e-9) return static_cast<int>(i);
  return -1;
}

}  // namespace

namespace internal {

NestResult latticeFill(const NestRequest& request, const NestContext& context,
                       NfpCache& cache, const LatticeOptions& options) {
  NestResult best;
  if (request.designs.size() != 1 || !context.valid) return best;

  const int quantity = request.designs[0].quantity;
  if (quantity <= 0) return best;

  const std::vector<Polygon>& shapes = context.shapes[0];
  const std::vector<double>& angles = context.angles[0];

  const auto nfpFor = [&](int a, int b) -> const Polygons& {
    return cache.get(NfpKey{0, a, 0, b}, [&] {
      return noFitPolygon(shapes[static_cast<std::size_t>(a)],
                          shapes[static_cast<std::size_t>(b)]);
    });
  };

  for (int r : sampleRotations(shapes.size(), options.maxRotations)) {
    const Polygons& selfNfp = nfpFor(r, r);
    if (selfNfp.empty()) continue;

    // Tightest horizontal spacing: where a copy sliding right along the row
    // finally leaves the no-fit polygon.
    const Rect selfBox = bounds(selfNfp);
    const Blocker selfBlocker = makeBlocker(selfNfp);
    double pitchX = 0.0;
    {
      // Bisect rather than ray cast: a concave piece can leave the region and
      // re-enter, and only the last crossing is a legal spacing.
      double lo = 0.0;
      double hi = selfBox.maxX + 1.0;
      for (int i = 0; i < 40; ++i) {
        const double mid = (lo + hi) * 0.5;
        if (selfBlocker.blocks(Point{mid, 0.0}))
          lo = mid;
        else
          hi = mid;
      }
      pitchX = hi;
    }
    if (!(pitchX > 0.0)) continue;

    std::vector<int> alternates{r};
    if (options.allowAlternation) {
      const int flipped = rotationOffsetBy(angles, r, 180.0);
      if (flipped >= 0 && flipped != r) alternates.push_back(flipped);
    }

    std::vector<double> staggers{0.0};
    if (options.allowStagger) staggers.push_back(pitchX * 0.5);

    for (int alternate : alternates) {
      Blockers blockers;
      blockers.pair[0][0] = makeBlocker(nfpFor(r, r));
      blockers.pair[0][1] = makeBlocker(nfpFor(r, alternate));
      blockers.pair[1][0] = makeBlocker(nfpFor(alternate, r));
      blockers.pair[1][1] = makeBlocker(nfpFor(alternate, alternate));
      for (int a = 0; a < 2; ++a)
        for (int b = 0; b < 2; ++b) {
          blockers.maxWidth =
              std::max(blockers.maxWidth, blockers.pair[a][b].box.width());
          blockers.maxHeight =
              std::max(blockers.maxHeight, blockers.pair[a][b].box.height());
        }

      const Polygon baseIfp = innerFitRectangle(
          context.usable, shapes[static_cast<std::size_t>(r)]);
      const Polygon alternateIfp = innerFitRectangle(
          context.usable, shapes[static_cast<std::size_t>(alternate)]);
      if (baseIfp.empty() || alternateIfp.empty()) continue;

      for (double stagger : staggers) {
        Tiling tiling{r, alternate, pitchX, 0.0, stagger};
        tiling.fit[0] = bounds(baseIfp);
        tiling.fit[1] = bounds(alternateIfp);
        tiling.pitchY = tightestPitchY(tiling, blockers);
        if (!(tiling.pitchY > 0.0)) continue;

        NestResult candidate = layoutTiling(request, context, tiling, quantity);
        if (betterLayout(candidate, best)) best = std::move(candidate);
      }
    }
  }
  return best;
}

}  // namespace internal

NestResult latticeFill(const NestRequest& request,
                       const LatticeOptions& options) {
  const internal::NestContext context = internal::buildContext(request);
  NfpCache cache;
  return internal::latticeFill(request, context, cache, options);
}

}  // namespace nest
