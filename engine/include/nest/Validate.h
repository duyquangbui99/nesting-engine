#pragma once
// The hard gate before anything reaches a document. Section 8 of
// OUTLINE_NESTING_SPEC.md.
//
// Everything upstream works on a simplified stand-in outline for speed. This
// runs on the exact gap-offset outlines, so it is the only place that knows
// what will actually be cut. The engine is allowed to produce a worse layout
// than it might have; it is never allowed to produce one that overlaps or runs
// off the sheet. When this reports anything, Apply must be refused and the
// offending instance named.

#include <string>
#include <vector>

#include "nest/Nest.h"

namespace nest {

enum class ViolationKind {
  Overlap,             // two placed outlines interpenetrate
  OutsideSheet,        // a placed outline leaves the usable region
  RotationNotAllowed,  // an angle the design's rotation mode forbids
  UnknownInstance,     // a placement naming a design or copy never requested
  DuplicateInstance,   // the same copy placed twice
  CountMismatch,       // placed plus unplaced does not equal requested
  SheetCountWrong,     // a sheet index outside the reported sheet count
};

std::string describe(ViolationKind kind);

struct Violation {
  ViolationKind kind{ViolationKind::Overlap};
  int designIndex{-1};
  int instanceIndex{-1};
  int sheetIndex{-1};
  // The second party to an overlap.
  int otherDesignIndex{-1};
  int otherInstanceIndex{-1};
  // Penetration depth for an overlap, distance past the edge for a piece off
  // the sheet, zero for the bookkeeping failures.
  double magnitudeMm{0.0};
  std::string message;
};

struct ValidationReport {
  std::vector<Violation> violations;
  int requested{0};
  int placed{0};
  double worstPenetrationMm{0.0};
  double worstOverrunMm{0.0};
  // Pairs actually tested for overlap. Far below the square of the piece count
  // when the grid is doing its job.
  std::size_t pairsTested{0};

  bool ok() const { return violations.empty(); }
};

// `tolerance` is the contact allowance: two pieces placed exactly touching land
// on Clipper2's coordinate grid and read as a sliver a fraction of a micron
// deep, which is not an overlap.
ValidationReport validate(const NestRequest& request, const NestResult& result,
                          double tolerance = kPlacementToleranceMm);

}  // namespace nest
