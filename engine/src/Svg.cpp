#include "nest/Svg.h"

#include <fstream>
#include <iomanip>
#include <sstream>

namespace nest {

void SvgWriter::add(const Polygon& ring, const Style& style) {
  if (ring.size() >= 3) items_.push_back(Item{ring, style});
}

void SvgWriter::add(const Polygons& rings, const Style& style) {
  for (const Polygon& ring : rings) add(ring, style);
}

void SvgWriter::addLabel(const Point& at, const std::string& text) {
  labels_.push_back(Label{at, text});
}

std::string SvgWriter::toString() const {
  Rect box;
  for (const Item& item : items_)
    for (const Point& p : item.ring) box.extend(p);
  for (const Label& label : labels_) box.extend(label.at);
  if (!box.valid()) box = Rect{0, 0, 1, 1};

  const double pad = std::max(1.0, std::max(box.width(), box.height()) * 0.02);
  const double minX = box.minX - pad;
  const double minY = box.minY - pad;
  const double w = box.width() + 2 * pad;
  const double h = box.height() + 2 * pad;

  // SVG y grows downward; our geometry grows upward. Flip once, around the
  // top of the view box, so shapes read the way they do in Illustrator.
  const double flip = 2.0 * minY + h;

  std::ostringstream out;
  out << std::fixed << std::setprecision(4);
  out << "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"" << minX << ' '
      << minY << ' ' << w << ' ' << h << "\" width=\"" << w * 4 << "\" height=\""
      << h * 4 << "\">\n";
  out << "<rect x=\"" << minX << "\" y=\"" << minY << "\" width=\"" << w
      << "\" height=\"" << h << "\" fill=\"#ffffff\"/>\n";

  for (const Item& item : items_) {
    out << "<polygon points=\"";
    for (const Point& p : item.ring) out << p.x << ',' << (flip - p.y) << ' ';
    out << "\" fill=\"" << item.style.fill << "\" fill-opacity=\""
        << item.style.fillOpacity << "\" stroke=\"" << item.style.stroke
        << "\" stroke-width=\"" << item.style.strokeWidth << "\"/>\n";
  }
  for (const Label& label : labels_) {
    out << "<text x=\"" << label.at.x << "\" y=\"" << (flip - label.at.y)
        << "\" font-family=\"sans-serif\" font-size=\"" << (pad * 2)
        << "\" fill=\"#444444\">" << label.text << "</text>\n";
  }
  out << "</svg>\n";
  return out.str();
}

bool SvgWriter::write(const std::string& path) const {
  std::ofstream file(path);
  if (!file) return false;
  file << toString();
  return file.good();
}

}  // namespace nest
