#pragma once
// Debug output. Every stage of the engine is judged by looking at an SVG.

#include <string>
#include <vector>

#include "nest/Types.h"

namespace nest {

class SvgWriter {
 public:
  struct Style {
    std::string fill{"none"};
    std::string stroke{"#111111"};
    double strokeWidth{0.2};
    double fillOpacity{1.0};
  };

  void add(const Polygon& ring, const Style& style);
  void add(const Polygons& rings, const Style& style);
  void addLabel(const Point& at, const std::string& text);

  std::string toString() const;
  bool write(const std::string& path) const;

 private:
  struct Item {
    Polygon ring;
    Style style;
  };
  struct Label {
    Point at;
    std::string text;
  };
  std::vector<Item> items_;
  std::vector<Label> labels_;
};

}  // namespace nest
