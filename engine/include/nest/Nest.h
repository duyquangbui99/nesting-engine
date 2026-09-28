#pragma once
// Single-sheet bottom-left-fill. Section 5 of OUTLINE_NESTING_SPEC.md.
//
// Pieces are placed one at a time in the order given. For each piece the
// engine builds the region where it may legally go, which is the inner-fit
// rectangle of the sheet minus the no-fit polygons of everything already
// placed, then takes the extreme vertex of that region in the fill direction.
//
// There is no search here yet. A single pass is fast but sensitive to the
// order it is handed; the guided search that fixes that arrives at step 6.

#include <string>
#include <vector>

#include "nest/NfpCache.h"
#include "nest/Types.h"

namespace nest {

// Which corner of the sheet pieces are packed towards.
enum class FillDirection { BottomLeft, TopLeft, BottomRight, TopRight };

// Per-design rotation constraint. Stickers with a fixed print direction pin to
// zero, or to zero and 180.
enum class RotationMode { Free, FixedZero, ZeroOr180 };

struct Design {
  std::string id;
  // A single solid ring, already grown by half the gap. This is what
  // buildOutline returns, so the gap is baked into the geometry and the placer
  // never reasons about clearance again.
  Polygon outline;
  int quantity{1};
  RotationMode rotation{RotationMode::Free};
};

struct Placement {
  int designIndex{0};
  int instanceIndex{0};
  int sheetIndex{0};
  double x{0.0};
  double y{0.0};
  double rotationDeg{0.0};
};

struct Sheet {
  double width{0.0};
  double height{0.0};
  double marginLeft{0.0};
  double marginRight{0.0};
  double marginTop{0.0};
  double marginBottom{0.0};

  // The sheet inset by its margins, then inset again by half the gap so that
  // outlines already grown by half the gap only need to sit inside it.
  Rect usable(double gap) const;
};

struct NestRequest {
  Sheet sheet;
  std::vector<Design> designs;
  double gap{0.0};
  // Coarse rotation step in degrees. The spec allows 90, 45, 30, 15, 10, 5, 2
  // and 1; any positive value that divides 360 works here.
  double rotationStepDeg{15.0};
  FillDirection direction{FillDirection::BottomLeft};
  // Outlines are replaced by searchOutline at this tolerance for collision
  // work. The stand-in always contains the real outline, so a layout it calls
  // legal stays legal. Zero collides the exact rings and is much slower.
  double searchToleranceMm{kDefaultToleranceMm};
  // Upper bound on the vertex count of the stand-in the search collides. The
  // cost of a no-fit polygon rises roughly as the fourth power of this, so it
  // is the single number that decides whether a sheet nests in a second or a
  // minute. Zero leaves it uncapped, which only suits simple artwork.
  std::size_t searchMaxVertices{60};
  // How many sheets may be opened. One is "fill this sheet and tell me what
  // did not fit", which is what the spec's fill-sheet mode needs. Raising it
  // starts a new sheet whenever the current one can take no more, one artboard
  // per sheet on commit.
  int maxSheets{1};
};

struct NestResult {
  std::vector<Placement> placements;
  int unplaced{0};
  // Placed outline area over usable sheet area. The outlines carry the gap, so
  // this reads a little higher than the material a cutter actually removes.
  double utilization{0.0};
  int sheetCount{0};
  // Bounding box of what sits on the last sheet. Compaction is measured here:
  // the tighter it is, the more of the part-used sheet is left over in one
  // usable piece. Earlier sheets are full, so their extent says nothing.
  Rect occupied;
  int refinementMoves{0};
  std::size_t nfpCacheHits{0};
  std::size_t nfpCacheMisses{0};
};

// The rotations a design may take at a given step, in degrees.
std::vector<double> rotationsFor(RotationMode mode, double stepDeg);

// A placement's outline in sheet coordinates.
Polygon placedOutline(const Design& design, const Placement& placement);

NestResult bottomLeftFill(const NestRequest& request);

}  // namespace nest
