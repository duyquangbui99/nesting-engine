# Outline Nesting Engine — v1 Specification

Date: 2026-09-10
Status: DECIDED. Supersedes the "rectangle-only Version 1" sections of `sticker_nesting_plugin_plan.md` (sections 11, 12, 16, 17, 38-40, 59, 65, 71, 72). Everything else in the plan still applies. Stack per `TECH_STACK_DECISION.md`.

## 1. Scope decision

Version 1 ships true-shape (outline) nesting with free rotation. There is no rectangle-only release first. Rectangle packing survives only as an internal fast path (section 6).

Target user: sticker shops. Not a general CNC/apparel nester. Features are limited to what a sticker sheet needs.

## 2. Definitions

- **Design**: one selected Illustrator group. Contains artwork and a cut contour. Immutable source.
- **Outline**: the closed polygon the nester collides. Derived from the cut contour when one exists, otherwise from the union of the group's visible geometry.
- **Instance**: one copy of a design to be placed.
- **Placement**: `{designId, instanceIndex, sheetIndex, x, y, rotationDeg}` with `rotationDeg` any real value.
- **Gap**: minimum clear distance between any two placed outlines, and between an outline and the usable sheet edge.

## 3. Rotation model ("all degrees")

Rotation is unrestricted in the output. The search works in two stages:

1. **Coarse search** over a rotation step chosen by the user. Allowed steps: 90, 45, 30, 15, 10, 5, 2, 1 degrees. Default 15°. Finer step means more candidate orientations and more time.
2. **Continuous refinement** after coarse placement: each placed instance is jiggled by small rotations (±step/2, shrinking) and translations while it stays collision-free and improves the objective. This produces final angles like 37.4°, which is what "free rotation" means in practice.

Per-design override: a design can be pinned to 0° only, or 0/180° only, for stickers with a fixed print direction. Default: free.

The UI must show the tradeoff honestly: "1° step: best yield, slowest."

## 4. Geometry pipeline

```
Illustrator group
  -> read cut contour path(s)  (fallback: all paths in group)
  -> flatten Bezier segments to polylines   tolerance default 0.05 mm (0.14 pt)
  -> Clipper2 Union -> outer boundary polygon(s) (holes discarded for stickers)
  -> simplify (Douglas-Peucker) at the same tolerance
  -> if polygon has > 300 vertices, simplify further for search only; keep exact for validation
  -> offset outward by gap/2 (Clipper2 InflatePaths, round joins)
  -> cache per (designId)
```

Sheet usable region = sheet rectangle inset by the four margins, then inset further by gap/2, so the offset outlines only need to be inside it.

Each rotation used in the search gets a cached rotated copy of the offset polygon plus its bounding box and convex hull.

## 5. Placement algorithm

**Core method: No-Fit Polygon (NFP) bottom-left-fill.** For a candidate piece B at rotation r and an already placed piece A, the NFP of (A, B_r) is the region of B's reference point positions where B touches or overlaps A. B may go anywhere outside the union of all NFPs and inside the Inner-Fit Polygon of the sheet. The engine picks the feasible point with the best objective (lowest, then leftmost, by default; direction configurable to match the fill direction setting).

- NFP computed as the Minkowski sum of A and -B via Clipper2 `MinkowskiSum` for convex pairs; concave pairs via convex decomposition of the smaller piece and union of partial sums. Results cached by `(designA, rotA, designB, rotB)` with rotations quantized to the step. This cache is why rotation is stepped in the coarse stage.
- Feasible-region computation uses Clipper2 Difference. Candidate points are the vertices of the feasible region.
- Spatial grid over placed pieces so only nearby pieces contribute NFPs.

**Outer search: guided randomized ordering.** A single bottom-left pass is fast but order-sensitive. The engine runs many passes with different orderings and rotation assignments and keeps the best, using a genetic algorithm over `(order, rotation index per instance)` in the style of the MIT-licensed SVGnest and Deepnest projects. Initial population seeds: largest area first, longest side first, per-design grouping, random. Deterministic seed by default.

**Quantities.** Identical designs are placed as instances of one cached outline, so 500 copies of one design cost 500 placements, not 500 geometry extractions. For identical pieces the engine first tries a lattice/tiling pass (rows with 180° alternation and staggered offsets) because for stickers this is often optimal and takes milliseconds; the result enters the population as a seed.

**Multiple sheets.** When a sheet fills, open the next. Objective order: fewer sheets first, then higher utilization on the last sheet, then compactness (bounding box of placements) so leftover material stays usable.

## 6. Fast path

If every design's outline covers at least 92% of its own bounding box (rectangles, rounded rectangles, squares), the engine switches to the rectangle MaxRects packer from the original plan with 0/90/180/270 rotations. Same `NestResult`, same validation. This makes rectangular sticker jobs instant on any hardware and is not exposed as a user choice beyond a "force outline mode" checkbox.

## 7. Time budget and hardware scaling

The engine takes a time budget, not a fixed iteration count.

| Quality preset | Budget | Rotation step default |
|---|---|---|
| Fast | 2 s | 45° |
| Balanced (default) | 10 s | 15° |
| Best | 60 s | 5° |
| Custom | user seconds | user step |

- Worker threads = `hardware_concurrency() - 1`, minimum 1, maximum 16. A 2-core laptop runs one search worker; a 16-core workstation runs 15. Same code, same result quality per second per core.
- Memory: NFP cache capped at 512 MB, least-recently-used eviction. Polygon simplification for search keeps vertex counts bounded.
- First feasible layout is available within about 200 ms for 500 pieces; the UI shows it and the utilization number immediately and keeps improving until the budget ends or the user clicks Stop. Stop always returns the best layout so far.
- Cancellation is checked between placements, never mid-geometry-operation, so Stop responds within roughly 50 ms.

Minimum tested hardware: 4 cores, 8 GB RAM. The engine must produce a valid layout for 1,000 instances within the Fast preset on that machine.

## 8. Validation (hard gate before Apply)

Run on exact, un-simplified, gap-offset outlines:

1. Every requested instance is placed exactly once.
2. No pair of placed outlines intersects (Clipper2 Intersection area == 0, pairs found via spatial grid).
3. Every outline lies inside the usable sheet region.
4. Every rotation respects the design's rotation constraint.
5. `requestedCount == placedCount` per design.

Any failure blocks Apply and reports which instance failed. Validation of 1,000 pieces must finish in under 300 ms.

## 9. Illustrator commit

Per placement: duplicate the source group, then apply one matrix = translate(-sourceCenter) × rotate(rotationDeg) × translate(targetPoint). Artwork and cut contour stay in one group, so both receive the same matrix. Output goes to one artboard per sheet inside a `STICKER NEST OUTPUT` layer, preserving the source layer split for print and cut if the user enables "keep layer structure". One undo step.

## 10. Settings exposed in v1

Sheet width, height, unit (in/mm/pt). Margins L/R/T/B. Gap. Rotation: Free (step selector) / 0 only / 0 and 180. Fill direction: bottom-left, top-left, bottom-right, top-right. Quality preset. Per design: quantity or "fill sheet". Checkboxes: force outline mode, keep layer structure, allow same design only per sheet (off).

Not in v1: nesting inside holes, bleed generation, registration marks, presets, labels.

## 11. Libraries

| Library | Use | License |
|---|---|---|
| Clipper2 | offset, union, difference, intersection, Minkowski sum | Boost Software License 1.0 |
| nlohmann/json | CLI I/O, presets later | MIT |
| Catch2 v3 | tests | BSL-1.0 |

Explicitly not used: libnest2d (LGPL, unsafe for a closed commercial plugin), any GPL nesting code, Boost.Geometry (not needed once Clipper2 covers the operations).

Algorithm references, ideas only, no code copied: SVGnest and Deepnest (MIT), the Burke et al. NFP orbiting method, Gomes and Oliveira GA+BL placement.

## 12. Test plan

Unit: Bezier flattening tolerance, polygon offset with gap, NFP of two convex shapes matches analytic result, NFP of concave pair, feasible-region vertex candidates, rotation cache keying, validation catches a planted overlap.

Scenario tests with known answers:
- 25 circles diameter 2 in, sheet 10 × 10 in, gap 0: must reach at least 22 on one sheet in Balanced (staggered rows fit 25 at ~90% coverage; grid fits 25 exactly here so both pass, the test checks no overlap and 1 sheet).
- 40 right triangles: Balanced must beat the rectangle-packer sheet count.
- 200 mixed die-cut shapes from a fixture file: utilization must not regress below the stored baseline by more than 1 percentage point.
- Every scenario: validation passes, deterministic seed reproduces the same result.

Benchmarks: 100 / 500 / 1,000 / 5,000 instances at steps 45°, 15°, 5°. Record first-feasible time, utilization at 2 s / 10 s / 60 s, peak memory. Run on the 4-core reference machine.

## 13. Build order

1. Toolchain spike: build an Illustrator SDK sample and load it in Illustrator 2026 on Windows.
2. Geometry: polygon types, flattening, Clipper2 integration, offset, union, simplification, tests.
3. NFP: convex Minkowski, concave decomposition, cache, tests against analytic cases.
4. Single-sheet bottom-left-fill with fixed order and stepped rotation. First visual results via the CLI writing SVG previews.
5. Continuous refinement (jiggle) stage.
6. Guided search (GA over order and rotation), thread pool, time budget, cancellation, deterministic seed.
7. Multi-sheet, fast path for near-rectangular designs, lattice seed for identical pieces.
8. Validation gate and benchmarks on the 4-core reference machine.
9. Illustrator adapter: extract cut contour, flatten, commit with arbitrary rotation, one undo step.
10. Native dialog with live-improving preview and Stop.

Steps 2 through 8 run entirely in the CLI with SVG output and need no Illustrator.

## 14. Honest expectations

Steps 2 through 8 are the bulk of the project and are computational geometry, not Adobe work. Expect several months of part-time effort with AI assistance before the engine is trustworthy on real customer files, and expect edge cases in path flattening and concave NFPs to consume most of the debugging time. The validation gate in section 8 is what protects customers while that matures: the plugin may occasionally produce a worse layout, but it must never produce an overlapping or out-of-bounds one.
