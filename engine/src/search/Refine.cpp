#include "nest/Refine.h"

#include <algorithm>
#include <cmath>
#include <optional>

#include "nest/Clipping.h"
#include "nest/Outline.h"
#include "nest/Transform.h"

namespace nest {
namespace {

// How far a slide is refined once it is known to be blocked at full length.
// Five halvings resolve a 4 mm attempt to about a tenth of a millimetre.
constexpr int kSlideHalvings = 5;

// A move must improve the objective by at least this much to be worth taking.
// Below it the layout is only churning.
constexpr double kMinimumGainMm = 1e-4;

struct Piece {
  Polygon collide;  // search outline at the current angle, in sheet coordinates
  Rect box;
  // Bounds of the real outline, which is what the layout is finally measured
  // by. Kept apart from `box` because collision runs on the slightly larger
  // stand-in and the two are not interchangeable for reporting.
  Rect trueBox;
  double x{0.0};
  double y{0.0};
  double angleDeg{0.0};
  int designIndex{0};
};

// Unit vector pointing at the corner the layout packs towards.
Point fillVector(FillDirection direction) {
  switch (direction) {
    case FillDirection::BottomLeft:
      return Point{-1.0, -1.0};
    case FillDirection::BottomRight:
      return Point{1.0, -1.0};
    case FillDirection::TopLeft:
      return Point{-1.0, 1.0};
    case FillDirection::TopRight:
      return Point{1.0, 1.0};
  }
  return Point{-1.0, -1.0};
}

// How far into the sheet a piece sits, measured along the fill direction.
// Smaller is tighter. The primary axis dominates so that the ordering matches
// the one bottom-left-fill used to choose positions in the first place.
double score(const Rect& box, FillDirection direction) {
  const Point dir = fillVector(direction);
  const double primary = dir.y < 0.0 ? box.minY : -box.maxY;
  const double secondary = dir.x < 0.0 ? box.minX : -box.maxX;
  return primary * 1000.0 + secondary;
}

bool insideSheet(const Rect& box, const Rect& usable) {
  return box.minX >= usable.minX - kPlacementToleranceMm &&
         box.minY >= usable.minY - kPlacementToleranceMm &&
         box.maxX <= usable.maxX + kPlacementToleranceMm &&
         box.maxY <= usable.maxY + kPlacementToleranceMm;
}

bool boxesApart(const Rect& a, const Rect& b) {
  return a.maxX < b.minX || b.maxX < a.minX || a.maxY < b.minY ||
         b.maxY < a.minY;
}

// True when `candidate` may take the place of piece `self` without hitting
// anything. The bounding box test skips almost every pair, which is what keeps
// this affordable inside a search loop.
bool fits(const std::vector<Piece>& pieces, std::size_t self,
          const Polygon& candidate, const Rect& candidateBox,
          const Rect& usable) {
  if (!insideSheet(candidateBox, usable)) return false;
  for (std::size_t i = 0; i < pieces.size(); ++i) {
    if (i == self) continue;
    if (boxesApart(candidateBox, pieces[i].box)) continue;
    if (overlaps(candidate, pieces[i].collide)) return false;
  }
  return true;
}

}  // namespace

NestResult refine(const NestRequest& request, const NestResult& coarse,
                  const RefineOptions& options) {
  NestResult result = coarse;
  result.refinementMoves = 0;
  if (coarse.placements.empty()) return result;

  const Rect usable = request.sheet.usable(request.gap);
  const double rotationSeed = options.rotationDeg > 0.0
                                  ? options.rotationDeg
                                  : request.rotationStepDeg * 0.5;

  std::vector<Polygon> collideOutline(request.designs.size());
  for (std::size_t d = 0; d < request.designs.size(); ++d)
    collideOutline[d] =
        searchOutline(request.designs[d].outline, request.searchToleranceMm,
                      request.searchMaxVertices);

  const auto shapeAt = [&](int designIndex, double angleDeg, double x,
                           double y) {
    return translated(rotated(collideOutline[designIndex], angleDeg),
                      Point{x, y});
  };

  const auto trueBoundsAt = [&](int designIndex, double angleDeg, double x,
                                double y) {
    return bounds(translated(rotated(request.designs[designIndex].outline,
                                     angleDeg),
                             Point{x, y}));
  };

  std::vector<Piece> pieces;
  pieces.reserve(coarse.placements.size());
  for (const Placement& p : coarse.placements) {
    Polygon ring = shapeAt(p.designIndex, p.rotationDeg, p.x, p.y);
    const Rect box = bounds(ring);
    pieces.push_back(Piece{std::move(ring), box,
                           trueBoundsAt(p.designIndex, p.rotationDeg, p.x, p.y),
                           p.x, p.y, p.rotationDeg, p.designIndex});
  }

  // The layout's overall extent, and what it would become if one piece moved.
  // A move that helps a single piece can still widen the sheet's footprint:
  // sliding down by a hair while sliding right by millimetres improves that
  // piece's score, because the primary axis dominates it, and loosens
  // everything. Guarding on the global box is what makes "refinement never
  // loosens a layout" true rather than merely usual.
  const auto globalBox = [&](std::size_t replaced, const Rect& replacement) {
    Rect box;
    for (std::size_t i = 0; i < pieces.size(); ++i) {
      const Rect& contribution = i == replaced ? replacement : pieces[i].trueBox;
      box.extend(Point{contribution.minX, contribution.minY});
      box.extend(Point{contribution.maxX, contribution.maxY});
    }
    return box;
  };

  // Work on the piece furthest from the fill corner first. Moving it frees
  // room for everything behind it in the same pass.
  std::vector<std::size_t> order(pieces.size());
  for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;

  const Point dir = fillVector(request.direction);

  for (int round = 0; round < options.rounds; ++round) {
    const double slide = options.translationMm / std::pow(2.0, round);
    const double turn = rotationSeed / std::pow(2.0, round);
    bool movedAny = false;

    std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
      return score(pieces[a].box, request.direction) >
             score(pieces[b].box, request.direction);
    });

    for (std::size_t index : order) {
      Piece& piece = pieces[index];
      const bool mayTurn =
          request.designs[piece.designIndex].rotation == RotationMode::Free;

      // Each entry is a turn to try before sliding. Turning alone rarely pays;
      // turning and then sliding into the room it opened does.
      std::vector<double> turns{0.0};
      if (mayTurn && turn > 0.0) {
        turns.push_back(turn);
        turns.push_back(-turn);
      }

      double bestScore = score(piece.box, request.direction);
      double bestGlobalArea = globalBox(pieces.size(), Rect{}).area();
      std::optional<Piece> bestPiece;

      for (double delta : turns) {
        const double angle = piece.angleDeg + delta;

        // Turn about the piece where it stands rather than about the origin,
        // otherwise a small rotation flings it across the sheet.
        Point anchor{piece.x, piece.y};
        if (delta != 0.0) {
          const Rect rotatedBox =
              bounds(rotated(collideOutline[piece.designIndex], piece.angleDeg));
          const Point centre{(rotatedBox.minX + rotatedBox.maxX) * 0.5,
                             (rotatedBox.minY + rotatedBox.maxY) * 0.5};
          const Point turned = rotate(centre, delta);
          anchor = Point{piece.x + centre.x - turned.x,
                         piece.y + centre.y - turned.y};
        }

        Polygon turnedRing =
            shapeAt(piece.designIndex, angle, anchor.x, anchor.y);
        Rect turnedBox = bounds(turnedRing);
        if (!fits(pieces, index, turnedRing, turnedBox, usable)) continue;

        // Slide as far towards the fill corner as the neighbours allow, on
        // each axis and then diagonally.
        Point at = anchor;
        Polygon ring = std::move(turnedRing);
        Rect box = turnedBox;
        const Point steps[3] = {
            Point{dir.x, 0.0}, Point{0.0, dir.y}, Point{dir.x, dir.y}};

        for (const Point& step : steps) {
          double lo = 0.0;
          double hi = slide;
          const auto tryAt = [&](double distance) {
            const Point candidateAt{at.x + step.x * distance,
                                    at.y + step.y * distance};
            Polygon candidate = shapeAt(piece.designIndex, angle, candidateAt.x,
                                        candidateAt.y);
            Rect candidateBox = bounds(candidate);
            if (!fits(pieces, index, candidate, candidateBox, usable))
              return false;
            ring = std::move(candidate);
            box = candidateBox;
            at = candidateAt;
            return true;
          };

          if (!tryAt(hi)) {
            // Blocked at full stretch: close in on how far it will actually go.
            for (int k = 0; k < kSlideHalvings; ++k) {
              const double mid = (lo + hi) * 0.5;
              if (tryAt(mid))
                lo = mid;
              else
                hi = mid;
            }
          }
        }

        const double candidateScore = score(box, request.direction);
        if (candidateScore >= bestScore - kMinimumGainMm) continue;

        const Rect candidateTrueBox =
            trueBoundsAt(piece.designIndex, angle, at.x, at.y);
        if (globalBox(index, candidateTrueBox).area() > bestGlobalArea + 1e-9)
          continue;

        bestScore = candidateScore;
        bestPiece = Piece{std::move(ring), box, candidateTrueBox, at.x, at.y,
                          angle, piece.designIndex};
      }

      if (bestPiece) {
        piece = std::move(*bestPiece);
        ++result.refinementMoves;
        movedAny = true;
      }
    }

    if (!movedAny) break;
  }

  result.occupied = Rect{};
  for (std::size_t i = 0; i < pieces.size(); ++i) {
    result.placements[i].x = pieces[i].x;
    result.placements[i].y = pieces[i].y;
    result.placements[i].rotationDeg = pieces[i].angleDeg;
    result.occupied.extend(Point{pieces[i].trueBox.minX, pieces[i].trueBox.minY});
    result.occupied.extend(Point{pieces[i].trueBox.maxX, pieces[i].trueBox.maxY});
  }
  return result;
}

}  // namespace nest
