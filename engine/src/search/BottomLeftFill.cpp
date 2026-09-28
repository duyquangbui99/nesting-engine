#include <algorithm>
#include <cmath>
#include <optional>

#include "nest/Clipping.h"
#include "nest/Nest.h"
#include "nest/Nfp.h"
#include "nest/Outline.h"
#include "nest/Transform.h"

#include "Placement.h"

namespace nest {
namespace {

// Two candidate positions whose coordinates differ by less than this are
// treated as equal, so the tie-break axis decides between them.
constexpr double kPositionEpsilon = 1e-7;

struct Candidate {
  Point position;
  double rotationDeg{0.0};
  int rotationIndex{0};
};

// Where two segments cross, if they do. Collinear overlaps are left out: their
// endpoints are already vertices, so nothing is lost.
std::optional<Point> segmentIntersection(const Point& p1, const Point& p2,
                                         const Point& p3, const Point& p4) {
  const double d1x = p2.x - p1.x, d1y = p2.y - p1.y;
  const double d2x = p4.x - p3.x, d2y = p4.y - p3.y;
  const double denom = d1x * d2y - d1y * d2x;
  if (std::abs(denom) < 1e-12) return std::nullopt;

  const double t = ((p3.x - p1.x) * d2y - (p3.y - p1.y) * d2x) / denom;
  const double u = ((p3.x - p1.x) * d1y - (p3.y - p1.y) * d1x) / denom;
  if (t < 0.0 || t > 1.0 || u < 0.0 || u > 1.0) return std::nullopt;
  return Point{p1.x + t * d1x, p1.y + t * d1y};
}

// Orders two candidate positions for a fill direction. Returns true when `a`
// is the better place to put a piece.
bool better(const Point& a, const Point& b, FillDirection direction) {
  const bool wantLow = direction == FillDirection::BottomLeft ||
                       direction == FillDirection::BottomRight;
  const bool wantLeft = direction == FillDirection::BottomLeft ||
                        direction == FillDirection::TopLeft;

  const double primaryA = wantLow ? a.y : -a.y;
  const double primaryB = wantLow ? b.y : -b.y;
  if (std::abs(primaryA - primaryB) > kPositionEpsilon)
    return primaryA < primaryB;

  const double secondaryA = wantLeft ? a.x : -a.x;
  const double secondaryB = wantLeft ? b.x : -b.x;
  return secondaryA < secondaryB;
}

// Everything already on the sheet, kept alongside what it took to get there so
// the no-fit polygons can be keyed and translated.
struct Placed {
  int designIndex{0};
  int rotationIndex{0};
  Point position;
  Polygon outline;  // in sheet coordinates
};

// A design's outline at one rotation, in the design's own coordinates.
struct RotatedOutline {
  Polygon ring;
  Rect box;
};

// Positions denied to one design at one rotation, grown as pieces land.
struct BlockedRegion {
  Polygons rings;
  std::size_t folded{0};  // how many placed pieces are already included
};

}  // namespace

Rect Sheet::usable(double gap) const {
  const double inset = gap * 0.5;
  return Rect{marginLeft + inset, marginBottom + inset,
              width - marginRight - inset, height - marginTop - inset};
}

std::vector<double> rotationsFor(RotationMode mode, double stepDeg) {
  switch (mode) {
    case RotationMode::FixedZero:
      return {0.0};
    case RotationMode::ZeroOr180:
      return {0.0, 180.0};
    case RotationMode::Free:
      break;
  }
  if (!(stepDeg > 0.0) || stepDeg > 360.0) return {0.0};

  std::vector<double> out;
  const int count = static_cast<int>(std::round(360.0 / stepDeg));
  out.reserve(static_cast<std::size_t>(std::max(count, 1)));
  for (int i = 0; i < count; ++i) out.push_back(i * stepDeg);
  return out;
}

Polygon placedOutline(const Design& design, const Placement& placement) {
  return translated(rotated(design.outline, placement.rotationDeg),
                    Point{placement.x, placement.y});
}

namespace internal {

NestContext buildContext(const NestRequest& request) {
  NestContext context;
  context.usable = request.sheet.usable(request.gap);
  if (!context.usable.valid() || context.usable.width() <= 0.0 ||
      context.usable.height() <= 0.0)
    return context;

  const std::size_t designCount = request.designs.size();
  context.angles.resize(designCount);
  context.shapes.resize(designCount);
  context.designArea.resize(designCount);

  for (std::size_t d = 0; d < designCount; ++d) {
    const Design& design = request.designs[d];
    context.angles[d] = rotationsFor(design.rotation, request.rotationStepDeg);
    context.designArea[d] = area(design.outline);

    const Polygon collide =
        searchOutline(design.outline, request.searchToleranceMm,
                      request.searchMaxVertices);
    context.shapes[d].reserve(context.angles[d].size());
    for (double angle : context.angles[d])
      context.shapes[d].push_back(rotated(collide, angle));

  }
  context.valid = true;
  return context;
}

std::vector<Gene> naturalSequence(const NestRequest& request) {
  std::vector<Gene> sequence;
  for (std::size_t d = 0; d < request.designs.size(); ++d)
    for (int i = 0; i < request.designs[d].quantity; ++i)
      sequence.push_back(Gene{static_cast<int>(d), i, -1});
  return sequence;
}

NestResult placeSequence(const NestRequest& request, const NestContext& context,
                         const std::vector<Gene>& sequence, NfpCache& cache,
                         const std::atomic<bool>* cancel,
                         std::chrono::steady_clock::time_point deadline) {
  NestResult result;
  const bool watchClock = deadline != std::chrono::steady_clock::time_point{};
  const auto stopNow = [&] {
    if (cancel && cancel->load(std::memory_order_relaxed)) return true;
    return watchClock && std::chrono::steady_clock::now() >= deadline;
  };
  if (!context.valid) {
    result.unplaced = static_cast<int>(sequence.size());
    return result;
  }

  const Rect& usable = context.usable;
  double placedArea = 0.0;
  const int sheetLimit = std::max(request.maxSheets, 1);

  // Pieces that will not fit on the sheet being filled are held back rather
  // than abandoned: a large one may be impossible here while a smaller one
  // still slots in, and whatever is left starts the next sheet.
  std::vector<Gene> remaining = sequence;
  std::vector<Gene> deferred;
  std::vector<Placed> placed;
  bool stopped = false;

  for (int sheet = 0; sheet < sheetLimit && !remaining.empty(); ++sheet) {
    placed.clear();
    deferred.clear();
    const std::size_t placedBefore = result.placements.size();
    result.occupied = Rect{};

    // Positions denied to one design at one rotation, kept across placements.
    // Rebuilding it from every no-fit polygon on every attempt is quadratic and
    // was the whole cost of a nest; folding each new piece in once is linear.
    // `folded` records how many placed pieces are already accounted for, so a
    // design and rotation that never comes up is never paid for.
    std::vector<std::vector<BlockedRegion>> blocked(context.shapes.size());
    for (std::size_t d = 0; d < context.shapes.size(); ++d)
      blocked[d].resize(context.shapes[d].size());

    const auto blockedFor = [&](std::size_t d, std::size_t r) -> const Polygons& {
      BlockedRegion& region = blocked[d][r];
      if (region.folded == placed.size()) return region.rings;

      Polygons merged = std::move(region.rings);
      for (std::size_t i = region.folded; i < placed.size(); ++i) {
        const Placed& other = placed[i];
        const NfpKey key{other.designIndex, other.rotationIndex,
                         static_cast<int>(d), static_cast<int>(r)};
        const Polygons& nfp = cache.get(key, [&] {
          return noFitPolygon(
              context.shapes[other.designIndex][other.rotationIndex],
              context.shapes[d][r]);
        });
        for (const Polygon& ring : nfp)
          merged.push_back(translated(ring, other.position));
      }
      region.rings = unionAll(merged);
      region.folded = placed.size();
      return region.rings;
    };

    for (const Gene& gene : remaining) {
      if (stopNow()) {
        stopped = true;
        break;
      }

      const std::size_t d = static_cast<std::size_t>(gene.designIndex);
      const std::size_t rotationCount = context.shapes[d].size();
      const std::size_t firstRotation =
          gene.rotationIndex < 0 ? 0 : static_cast<std::size_t>(gene.rotationIndex);
      const std::size_t lastRotation =
          gene.rotationIndex < 0 ? rotationCount : firstRotation + 1;

      std::optional<Candidate> best;
      for (std::size_t r = firstRotation; r < lastRotation && r < rotationCount;
           ++r) {
        // Each angle may cost a fresh no-fit polygon, so the clock is checked
        // here too. Keeping whatever rotations have been tried is better than
        // abandoning the piece: `best` already holds the best of them.
        if (r > firstRotation && stopNow()) break;
        const Polygon& shape = context.shapes[d][r];

        const Polygon ifp = innerFitRectangle(usable, shape);
        if (ifp.empty()) continue;  // too big for the sheet at this rotation
        const Rect ifpBox = bounds(ifp);

        const Polygons& region = blockedFor(d, r);

        const auto consider = [&](const Point& c) {
          if (c.x < ifpBox.minX - kPlacementToleranceMm ||
              c.x > ifpBox.maxX + kPlacementToleranceMm ||
              c.y < ifpBox.minY - kPlacementToleranceMm ||
              c.y > ifpBox.maxY + kPlacementToleranceMm)
            return;
          if (!best || better(c, best->position, request.direction))
            best = Candidate{c, context.angles[d][r], static_cast<int>(r)};
        };

        // A piece may sit anywhere inside the inner-fit rectangle that is not
        // strictly inside the blocked region. The boundary itself is legal, and
        // is where good layouts live: a position on it means the piece touches a
        // neighbour exactly.
        //
        // Taking the vertices of ifp minus blocked would seem natural and is
        // wrong. When a run of pieces reaches the far edge of the sheet, the
        // strip of legal positions against that edge has zero width, and a
        // polygon difference drops it, losing whole columns. Reading the boundary
        // directly keeps them.
        //
        // Because the blocked rings come from a union, every vertex of theirs
        // lies on that region's boundary and so is feasible by construction.
        // Only the sheet corners have to be tested.
        for (const Polygon& ring : region)
          for (const Point& p : ring) consider(p);

        for (const Polygon& ring : region) {
          for (std::size_t i = 0, n = ring.size(); i < n; ++i) {
            const Point& a = ring[i];
            const Point& b = ring[(i + 1) % n];
            for (std::size_t k = 0; k < ifp.size(); ++k) {
              const std::optional<Point> hit =
                  segmentIntersection(a, b, ifp[k], ifp[(k + 1) % ifp.size()]);
              if (hit) consider(*hit);
            }
          }
        }

        for (const Point& corner : ifp)
          if (!containsPoint(region, corner)) consider(corner);
      }

      if (!best) {
        deferred.push_back(gene);
        continue;
      }

      Placement placement;
      placement.designIndex = gene.designIndex;
      placement.instanceIndex = gene.instanceIndex;
      placement.sheetIndex = sheet;
      placement.x = best->position.x;
      placement.y = best->position.y;
      placement.rotationDeg = best->rotationDeg;
      result.placements.push_back(placement);

      // Measured on the real outline, not the slightly larger stand-in the
      // search collides, so the number means what a reader expects. Rotation and
      // translation do not change area.
      placedArea += context.designArea[d];
      for (const Point& p :
           translated(rotated(request.designs[d].outline, best->rotationDeg),
                      best->position))
        result.occupied.extend(p);

      placed.push_back(Placed{gene.designIndex, best->rotationIndex,
                              best->position,
                              translated(context.shapes[d][best->rotationIndex],
                                         best->position)});
    }

    if (result.placements.size() > placedBefore) result.sheetCount = sheet + 1;
    // Nothing fitted at all: the pieces left are too big for an empty sheet, so
    // opening another would change nothing.
    if (stopped || deferred.size() == remaining.size()) break;
    remaining = deferred;
  }

  // Every gene is either on a sheet or it is not, so this holds however the
  // loop ended: sheets exhausted, nothing more fitting, or a cancellation part
  // way through.
  result.unplaced =
      static_cast<int>(sequence.size() - result.placements.size());

  const double usableArea = usable.area();
  result.utilization =
      usableArea > 0.0 && result.sheetCount > 0
          ? placedArea / (usableArea * result.sheetCount)
          : 0.0;
  result.nfpCacheHits = cache.hits();
  result.nfpCacheMisses = cache.misses();
  return result;
}

}  // namespace internal

NestResult bottomLeftFill(const NestRequest& request) {
  const internal::NestContext context = internal::buildContext(request);
  NfpCache cache;
  return internal::placeSequence(request, context,
                                 internal::naturalSequence(request), cache);
}

}  // namespace nest
