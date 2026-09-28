// The engine's entry point for the browser.
//
// One function in, one function out, JSON both ways. A richer binding through
// Embind would be tidier to call but ties the engine's headers to Emscripten's;
// a string keeps the boundary exactly as thin as the CLI's, and the engine
// stays a plain C++ library that has heard of neither Adobe nor JavaScript.
//
// Distances are millimetres throughout, as everywhere else in the engine. The
// caller converts from PDF points.

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <string>

#include <nlohmann/json.hpp>

#include "nest/Lattice.h"
#include "nest/Nest.h"
#include "nest/Outline.h"
#include "nest/Search.h"
#include "nest/Validate.h"

#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
#else
#define EMSCRIPTEN_KEEPALIVE
#endif

using nlohmann::json;

namespace {

nest::Point pointFrom(const json& value) {
  return nest::Point{value.at(0).get<double>(), value.at(1).get<double>()};
}

nest::BezierPath pathFrom(const json& value) {
  nest::BezierPath path;
  path.start = pointFrom(value.at("start"));
  path.closed = value.value("closed", true);
  for (const json& segment : value.at("segments")) {
    // Six numbers: both control points then the end point.
    path.segments.push_back(nest::CubicSegment{
        nest::Point{segment.at(0).get<double>(), segment.at(1).get<double>()},
        nest::Point{segment.at(2).get<double>(), segment.at(3).get<double>()},
        nest::Point{segment.at(4).get<double>(), segment.at(5).get<double>()}});
  }
  return path;
}

nest::RotationMode rotationFrom(const std::string& name) {
  if (name == "fixed") return nest::RotationMode::FixedZero;
  if (name == "zeroOr180") return nest::RotationMode::ZeroOr180;
  return nest::RotationMode::Free;
}

nest::FillDirection directionFrom(const std::string& name) {
  if (name == "topLeft") return nest::FillDirection::TopLeft;
  if (name == "bottomRight") return nest::FillDirection::BottomRight;
  if (name == "topRight") return nest::FillDirection::TopRight;
  return nest::FillDirection::BottomLeft;
}

json boundsTo(const nest::Rect& box) {
  if (!box.valid()) return nullptr;
  return json{{"minX", box.minX}, {"minY", box.minY},
              {"maxX", box.maxX}, {"maxY", box.maxY}};
}

std::string describeKind(nest::ViolationKind kind) { return nest::describe(kind); }

// Copies a string into memory the caller frees with nest_free. Returning a
// pointer into a static would be simpler and would break the moment two calls
// overlapped.
char* release(const std::string& text) {
  char* out = static_cast<char*>(std::malloc(text.size() + 1));
  if (!out) return nullptr;
  std::memcpy(out, text.c_str(), text.size() + 1);
  return out;
}

json runNest(const json& request) {
  nest::NestRequest nestRequest;

  const json& sheet = request.at("sheet");
  nestRequest.sheet.width = sheet.at("width").get<double>();
  nestRequest.sheet.height = sheet.at("height").get<double>();
  nestRequest.sheet.marginLeft = sheet.value("marginLeft", 0.0);
  nestRequest.sheet.marginRight = sheet.value("marginRight", 0.0);
  nestRequest.sheet.marginTop = sheet.value("marginTop", 0.0);
  nestRequest.sheet.marginBottom = sheet.value("marginBottom", 0.0);

  nestRequest.gap = request.value("gap", 0.0);
  nestRequest.rotationStepDeg = request.value("rotationStepDeg", 15.0);
  nestRequest.maxSheets = request.value("maxSheets", 1);
  nestRequest.direction = directionFrom(request.value("direction", "bottomLeft"));
  nestRequest.searchMaxVertices =
      request.value("searchMaxVertices", std::size_t{60});

  json designReports = json::array();
  for (const json& design : request.at("designs")) {
    std::vector<nest::BezierPath> paths;
    for (const json& path : design.at("paths")) paths.push_back(pathFrom(path));

    nest::OutlineOptions outlineOptions;
    outlineOptions.gap = nestRequest.gap;
    const nest::Polygons outline = nest::buildOutline(paths, outlineOptions);

    json report{{"id", design.value("id", "")},
                {"rings", outline.size()},
                {"usable", !outline.empty()}};
    if (!outline.empty()) {
      report["vertices"] = outline.front().size();
      report["areaMm2"] = nest::area(outline.front());
      // What the search will actually collide, which is the number that
      // decides how long this takes.
      double toleranceUsed = 0.0;
      report["searchVertices"] =
          nest::searchOutline(outline.front(), nestRequest.searchToleranceMm,
                              nestRequest.searchMaxVertices, &toleranceUsed)
              .size();
      report["searchToleranceMm"] = toleranceUsed;
      nest::Design entry;
      entry.id = design.value("id", "");
      entry.outline = outline.front();
      entry.quantity = design.value("quantity", 1);
      entry.rotation = rotationFrom(design.value("rotation", "free"));
      nestRequest.designs.push_back(std::move(entry));
    }
    report["designIndex"] =
        outline.empty() ? -1 : static_cast<int>(nestRequest.designs.size()) - 1;
    designReports.push_back(std::move(report));
  }

  if (nestRequest.designs.empty())
    return json{{"ok", false},
                {"error", "none of the supplied paths produced an outline"},
                {"designs", designReports}};

  nest::SearchOptions searchOptions;
  searchOptions.timeBudgetSeconds = request.value("timeBudgetSeconds", 2.0);
  searchOptions.seed = request.value("seed", 1u);
  if (request.contains("maxGenerations"))
    searchOptions.maxGenerations = request.at("maxGenerations").get<int>();

  const nest::SearchResult found = nest::guidedSearch(nestRequest, searchOptions);
  const nest::ValidationReport report = nest::validate(nestRequest, found.best);

  json placements = json::array();
  for (const nest::Placement& placement : found.best.placements)
    placements.push_back(json{{"designIndex", placement.designIndex},
                              {"instanceIndex", placement.instanceIndex},
                              {"sheetIndex", placement.sheetIndex},
                              {"x", placement.x},
                              {"y", placement.y},
                              {"rotationDeg", placement.rotationDeg}});

  json violations = json::array();
  for (const nest::Violation& violation : report.violations)
    violations.push_back(json{{"kind", describeKind(violation.kind)},
                              {"designIndex", violation.designIndex},
                              {"instanceIndex", violation.instanceIndex},
                              {"otherDesignIndex", violation.otherDesignIndex},
                              {"otherInstanceIndex", violation.otherInstanceIndex},
                              {"sheetIndex", violation.sheetIndex},
                              {"magnitudeMm", violation.magnitudeMm},
                              {"message", violation.message}});

  return json{
      {"ok", true},
      {"designs", designReports},
      {"placements", placements},
      {"unplaced", found.best.unplaced},
      {"sheetCount", found.best.sheetCount},
      {"utilization", found.best.utilization},
      {"occupied", boundsTo(found.best.occupied)},
      {"fromLattice", found.bestFromLattice},
      {"generations", found.generations},
      {"evaluations", found.evaluations},
      {"threadsUsed", found.threadsUsed},
      {"nfpBuilt", found.best.nfpCacheMisses},
      {"nfpReused", found.best.nfpCacheHits},
      {"elapsedSeconds", found.elapsedSeconds},
      {"firstLayoutSeconds", found.firstLayoutSeconds},
      {"validation",
       json{{"ok", report.ok()},
            {"requested", report.requested},
            {"placed", report.placed},
            {"worstPenetrationMm", report.worstPenetrationMm},
            {"worstOverrunMm", report.worstOverrunMm},
            {"pairsTested", report.pairsTested},
            {"violations", violations}}}};
}

}  // namespace

extern "C" {

EMSCRIPTEN_KEEPALIVE const char* nest_version() { return "0.1.0"; }

/** Takes a request as JSON, returns a result as JSON. Never throws into the
    caller: a malformed request comes back as {"ok":false,"error":...}. */
EMSCRIPTEN_KEEPALIVE char* nest_run(const char* requestJson) {
  try {
    return release(runNest(json::parse(requestJson)).dump());
  } catch (const std::exception& failure) {
    return release(json{{"ok", false}, {"error", failure.what()}}.dump());
  } catch (...) {
    return release(json{{"ok", false}, {"error", "unknown failure"}}.dump());
  }
}

EMSCRIPTEN_KEEPALIVE void nest_free(char* text) { std::free(text); }

}  // extern "C"
