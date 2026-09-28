#pragma once
// Continuous refinement. Section 3 of OUTLINE_NESTING_SPEC.md.
//
// Bottom-left-fill leaves every piece on a coarse rotation step and wherever it
// first landed. Refinement then nudges each piece: slide it towards the fill
// corner as far as it will go, and turn it by small amounts, keeping only moves
// that stay collision-free and tighten the layout. The angles that come out are
// arbitrary reals, which is what free rotation means in practice.
//
// Refinement never invalidates a layout. Every move is checked before it is
// taken, so the result is always at least as good and always still legal.

#include "nest/Nest.h"

namespace nest {

struct RefineOptions {
  // Largest slide attempted in the first round, in millimetres. Each round
  // halves it, so the pass starts coarse and finishes fine.
  double translationMm{4.0};
  // Largest turn attempted in the first round, in degrees. Zero derives it
  // from the request as half the coarse rotation step, which is the spec's
  // rule. Also halves each round.
  double rotationDeg{0.0};
  int rounds{6};
};

NestResult refine(const NestRequest& request, const NestResult& coarse,
                  const RefineOptions& options = {});

}  // namespace nest
