#pragma once
// Regular tilings of a single design. Section 5 of OUTLINE_NESTING_SPEC.md.
//
// A sheet of identical stickers is usually packed best by a plain repeating
// grid, optionally with alternate rows turned over and offset by half a pitch.
// The genetic search will find something like it eventually, but it takes
// seconds; a tiling takes milliseconds because the spacing comes straight out
// of the no-fit polygon rather than out of trial and error.
//
// The pitch is read off the no-fit polygon directly: the distance at which a
// copy of the piece stops overlapping the original is exactly where its
// boundary is crossed, so one ray cast per direction gives the tightest legal
// spacing with no search at all.

#include "nest/Nest.h"

namespace nest {

struct LatticeOptions {
  // Turn alternate rows by 180 degrees. Shapes with a wide end and a narrow
  // end nest far tighter this way.
  bool allowAlternation{true};
  // Offset alternate rows by half a column pitch, which is how circles reach
  // their densest packing.
  bool allowStagger{true};
  // How many angles from the rotation table to try. Each one costs several
  // no-fit polygons, and a tiling is only a starting guess for the search, so
  // there is no point examining every angle of a fine step.
  int maxRotations{8};
};

// Best tiling found, or an empty result when the request has more than one
// design or nothing fits. Never returns an overlapping layout.
NestResult latticeFill(const NestRequest& request,
                       const LatticeOptions& options = {});

}  // namespace nest
