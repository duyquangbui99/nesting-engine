#include "nest/Validate.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>

#include "nest/Clipping.h"
#include "nest/Transform.h"

namespace nest {
namespace {

struct Placed {
  Polygon outline;  // exact, gap-offset, in sheet coordinates
  Rect box;
  int designIndex{0};
  int instanceIndex{0};
  int sheetIndex{0};
};

std::uint64_t instanceKey(int designIndex, int instanceIndex) {
  return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(designIndex))
          << 32) |
         static_cast<std::uint32_t>(instanceIndex);
}

bool angleAllowed(RotationMode mode, double degrees) {
  const double wrapped = std::fmod(std::fmod(degrees, 360.0) + 360.0, 360.0);
  switch (mode) {
    case RotationMode::FixedZero:
      return std::abs(wrapped) < 1e-6;
    case RotationMode::ZeroOr180:
      return std::abs(wrapped) < 1e-6 || std::abs(wrapped - 180.0) < 1e-6;
    case RotationMode::Free:
      return true;
  }
  return true;
}

// A uniform grid over placements, one bucket per cell, so a piece is only
// compared with its neighbours. Testing every pair costs half a million
// comparisons at a thousand pieces and cannot meet the 300 ms budget; this
// brings it down to a few per piece.
class NeighbourGrid {
 public:
  NeighbourGrid(const std::vector<Placed>& pieces) {
    double widest = 0.0;
    for (const Placed& piece : pieces)
      widest = std::max({widest, piece.box.width(), piece.box.height()});
    // One piece per cell on average. Smaller cells mean each piece spans many
    // of them; larger ones put unrelated pieces together.
    cell_ = widest > 0.0 ? widest : 1.0;

    for (std::size_t i = 0; i < pieces.size(); ++i)
      forEachCell(pieces[i], [&](std::int64_t key) { buckets_[key].push_back(i); });
  }

  // Calls `visit(j)` for every piece that might touch `pieces[i]`, each at most
  // once, and only for j > i so a pair is offered a single time.
  template <typename Visit>
  void neighbours(const std::vector<Placed>& pieces, std::size_t i,
                  Visit visit) const {
    std::vector<std::size_t> seen;
    forEachCell(pieces[i], [&](std::int64_t key) {
      const auto it = buckets_.find(key);
      if (it == buckets_.end()) return;
      for (std::size_t j : it->second)
        if (j > i) seen.push_back(j);
    });
    std::sort(seen.begin(), seen.end());
    seen.erase(std::unique(seen.begin(), seen.end()), seen.end());
    for (std::size_t j : seen) visit(j);
  }

 private:
  template <typename Fn>
  void forEachCell(const Placed& piece, Fn fn) const {
    const std::int64_t minX = static_cast<std::int64_t>(
        std::floor(piece.box.minX / cell_));
    const std::int64_t maxX = static_cast<std::int64_t>(
        std::floor(piece.box.maxX / cell_));
    const std::int64_t minY = static_cast<std::int64_t>(
        std::floor(piece.box.minY / cell_));
    const std::int64_t maxY = static_cast<std::int64_t>(
        std::floor(piece.box.maxY / cell_));
    for (std::int64_t x = minX; x <= maxX; ++x)
      for (std::int64_t y = minY; y <= maxY; ++y)
        fn(key(piece.sheetIndex, x, y));
  }

  static std::int64_t key(int sheet, std::int64_t x, std::int64_t y) {
    // Sheets never interact, so the sheet index is part of the cell address.
    std::uint64_t h = static_cast<std::uint64_t>(sheet) * 0x9E3779B97F4A7C15ull;
    h ^= static_cast<std::uint64_t>(x) * 0xC2B2AE3D27D4EB4Full;
    h ^= static_cast<std::uint64_t>(y) * 0x165667B19E3779F9ull;
    return static_cast<std::int64_t>(h);
  }

  double cell_{1.0};
  std::unordered_map<std::int64_t, std::vector<std::size_t>> buckets_;
};

bool boxesApart(const Rect& a, const Rect& b, double tolerance) {
  return a.maxX < b.minX - tolerance || b.maxX < a.minX - tolerance ||
         a.maxY < b.minY - tolerance || b.maxY < a.minY - tolerance;
}

}  // namespace

std::string describe(ViolationKind kind) {
  switch (kind) {
    case ViolationKind::Overlap:
      return "pieces overlap";
    case ViolationKind::OutsideSheet:
      return "piece is outside the usable sheet";
    case ViolationKind::RotationNotAllowed:
      return "rotation is not allowed for this design";
    case ViolationKind::UnknownInstance:
      return "placement names a design or copy that was not requested";
    case ViolationKind::DuplicateInstance:
      return "the same copy is placed more than once";
    case ViolationKind::CountMismatch:
      return "placed plus unplaced does not equal the number requested";
    case ViolationKind::SheetCountWrong:
      return "a piece sits on a sheet the result does not report";
  }
  return "unknown";
}

ValidationReport validate(const NestRequest& request, const NestResult& result,
                          double tolerance) {
  ValidationReport report;
  for (const Design& design : request.designs) report.requested += design.quantity;
  report.placed = static_cast<int>(result.placements.size());

  const Rect usable = request.sheet.usable(request.gap);
  const auto designCount = static_cast<int>(request.designs.size());

  std::vector<Placed> pieces;
  pieces.reserve(result.placements.size());
  std::unordered_set<std::uint64_t> seen;
  std::vector<int> perDesign(request.designs.size(), 0);

  for (const Placement& placement : result.placements) {
    if (placement.designIndex < 0 || placement.designIndex >= designCount) {
      report.violations.push_back(
          Violation{ViolationKind::UnknownInstance, placement.designIndex,
                    placement.instanceIndex, placement.sheetIndex, -1, -1, 0.0,
                    "design index out of range"});
      continue;
    }
    const Design& design =
        request.designs[static_cast<std::size_t>(placement.designIndex)];

    if (placement.instanceIndex < 0 ||
        placement.instanceIndex >= design.quantity) {
      report.violations.push_back(
          Violation{ViolationKind::UnknownInstance, placement.designIndex,
                    placement.instanceIndex, placement.sheetIndex, -1, -1, 0.0,
                    "copy index outside the requested quantity"});
      continue;
    }

    if (!seen.insert(instanceKey(placement.designIndex, placement.instanceIndex))
             .second) {
      report.violations.push_back(
          Violation{ViolationKind::DuplicateInstance, placement.designIndex,
                    placement.instanceIndex, placement.sheetIndex, -1, -1, 0.0,
                    "this copy has already been placed"});
      continue;
    }
    ++perDesign[static_cast<std::size_t>(placement.designIndex)];

    if (!angleAllowed(design.rotation, placement.rotationDeg))
      report.violations.push_back(
          Violation{ViolationKind::RotationNotAllowed, placement.designIndex,
                    placement.instanceIndex, placement.sheetIndex, -1, -1,
                    placement.rotationDeg, "angle forbidden by the design"});

    // Exact outline, not the stand-in the search collided.
    Polygon outline = placedOutline(design, placement);
    const Rect box = bounds(outline);

    const double overrun = std::max(
        {usable.minX - box.minX, usable.minY - box.minY, box.maxX - usable.maxX,
         box.maxY - usable.maxY, 0.0});
    if (overrun > tolerance) {
      report.worstOverrunMm = std::max(report.worstOverrunMm, overrun);
      report.violations.push_back(
          Violation{ViolationKind::OutsideSheet, placement.designIndex,
                    placement.instanceIndex, placement.sheetIndex, -1, -1,
                    overrun, "piece extends past the usable sheet"});
    } else {
      report.worstOverrunMm = std::max(report.worstOverrunMm, 0.0);
    }

    pieces.push_back(Placed{std::move(outline), box, placement.designIndex,
                            placement.instanceIndex, placement.sheetIndex});
  }

  for (std::size_t d = 0; d < request.designs.size(); ++d) {
    const int quantity = request.designs[d].quantity;
    if (perDesign[d] > quantity)
      report.violations.push_back(
          Violation{ViolationKind::CountMismatch, static_cast<int>(d), -1, -1,
                    -1, -1, 0.0, "more copies placed than requested"});
  }

  // Every placement must name a sheet the result admits to having opened.
  // Caught a tiling that filled a sheet and reported none, which would have
  // created no artboard to commit onto.
  for (const Placed& piece : pieces) {
    if (piece.sheetIndex >= 0 && piece.sheetIndex < result.sheetCount) continue;
    report.violations.push_back(
        Violation{ViolationKind::SheetCountWrong, piece.designIndex,
                  piece.instanceIndex, piece.sheetIndex, -1, -1, 0.0,
                  "sheet index outside the reported sheet count"});
    break;
  }

  if (report.placed + result.unplaced != report.requested)
    report.violations.push_back(
        Violation{ViolationKind::CountMismatch, -1, -1, -1, -1, -1, 0.0,
                  "placed plus unplaced does not equal requested"});

  const NeighbourGrid grid(pieces);
  for (std::size_t i = 0; i < pieces.size(); ++i) {
    grid.neighbours(pieces, i, [&](std::size_t j) {
      if (pieces[i].sheetIndex != pieces[j].sheetIndex) return;
      if (boxesApart(pieces[i].box, pieces[j].box, tolerance)) return;
      ++report.pairsTested;

      const double depth = penetrationDepth(pieces[i].outline, pieces[j].outline);
      report.worstPenetrationMm = std::max(report.worstPenetrationMm, depth);
      if (depth <= tolerance) return;

      report.violations.push_back(
          Violation{ViolationKind::Overlap, pieces[i].designIndex,
                    pieces[i].instanceIndex, pieces[i].sheetIndex,
                    pieces[j].designIndex, pieces[j].instanceIndex, depth,
                    "these two pieces overlap"});
    });
  }

  return report;
}

}  // namespace nest
