#include "gb/export/framing.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <numbers>
#include <stdexcept>
#include <string_view>

namespace gb::exporting {
namespace {

constexpr std::array<std::string_view, 8> kViewNames{"rectilinear",   "fisheye",  "stereographic",
                                                     "little-planet", "panorama", "raw-1",
                                                     "raw-2",         "original"};

int even(const double value) { return std::max(2, 2 * static_cast<int>(std::lround(value / 2.0))); }

std::optional<int> whole(const std::string &text) {
  if (text.empty() || text.size() > 5U)
    return std::nullopt;
  for (const auto c : text) {
    if (std::isdigit(static_cast<unsigned char>(c)) == 0)
      return std::nullopt;
  }
  return std::stoi(text);
}

double radians(const double degrees) { return degrees * std::numbers::pi / 180.0; }

} // namespace

std::string AspectRatio::text() const {
  return std::to_string(width) + ":" + std::to_string(height);
}

std::optional<AspectRatio> parse_aspect(const std::string &text) {
  const auto colon = text.find(':');
  if (colon == std::string::npos)
    return std::nullopt;
  const auto width = whole(text.substr(0, colon));
  const auto height = whole(text.substr(colon + 1U));
  if (!width || !height || *width < 1 || *height < 1 || *width > 10000 || *height > 10000)
    return std::nullopt;
  const AspectRatio aspect{*width, *height};
  if (aspect.value() < 0.1 - 1e-12 || aspect.value() > 10.0 + 1e-12)
    return std::nullopt;
  return aspect;
}

std::string panel_side_name(const PanelSide side) {
  switch (side) {
  case PanelSide::left:
    return "left";
  case PanelSide::right:
    return "right";
  case PanelSide::top:
    return "top";
  case PanelSide::bottom:
    return "bottom";
  case PanelSide::none:
    break;
  }
  return "none";
}

std::optional<PanelSide> parse_panel_side(const std::string &text) {
  for (const auto side :
       {PanelSide::none, PanelSide::left, PanelSide::right, PanelSide::top, PanelSide::bottom}) {
    if (panel_side_name(side) == text)
      return side;
  }
  return std::nullopt;
}

std::optional<double> picture_aspect(const AspectRatio frame, const PanelSide panel,
                                     const double panel_size) {
  const auto aspect = frame.value();
  if (panel == PanelSide::none)
    return aspect;
  if (!(panel_size >= kMinimumPanelSize && panel_size <= kMaximumPanelSize))
    return std::nullopt;
  if (panel == PanelSide::top || panel == PanelSide::bottom)
    return panel_size < 0.95 ? std::optional<double>(aspect / (1.0 - panel_size)) : std::nullopt;
  const auto width = aspect - panel_size;
  // Keep at least a tenth of the frame for the picture.
  if (width < aspect * 0.1)
    return std::nullopt;
  return width;
}

FrameLayout frame_layout(const AspectRatio frame, const int width, const PanelSide panel,
                         const double panel_size) {
  if (width < 16 || width > 16384)
    throw std::invalid_argument("frame width must be within 16..16384 pixels");
  if (!picture_aspect(frame, panel, panel_size))
    throw std::invalid_argument("the panel leaves no room for the picture");
  FrameLayout layout;
  layout.width = even(width);
  layout.height = even(layout.width / frame.value());
  if (layout.height > 16384)
    throw std::invalid_argument("frame height must be at most 16384 pixels");
  layout.picture = {0, 0, layout.width, layout.height};
  if (panel == PanelSide::none)
    return layout;
  const auto size = std::lround(panel_size * layout.height);
  if (panel == PanelSide::top || panel == PanelSide::bottom) {
    const auto picture_height = even(layout.height - static_cast<double>(size));
    layout.picture.height = picture_height;
    if (panel == PanelSide::top) {
      layout.panel = PixelRect{0, 0, layout.width, layout.height - picture_height};
      layout.picture.y = layout.height - picture_height;
    } else {
      layout.panel = PixelRect{0, picture_height, layout.width, layout.height - picture_height};
    }
    return layout;
  }
  const auto picture_width = even(layout.width - static_cast<double>(size));
  layout.picture.width = picture_width;
  const auto panel_width = layout.width - picture_width;
  if (panel == PanelSide::left) {
    layout.panel = PixelRect{0, 0, panel_width, layout.height};
    layout.picture.x = panel_width;
  } else {
    layout.panel = PixelRect{picture_width, 0, panel_width, layout.height};
  }
  return layout;
}

PanelRegions panel_regions(const FrameLayout &layout, const PanelSide side, const bool map,
                           const bool text) {
  PanelRegions regions;
  if (!layout.panel || side == PanelSide::none || (!map && !text))
    return regions;
  const auto margin = static_cast<int>(std::lround(24.0 * layout.height / 1080.0));
  const auto &panel = *layout.panel;
  const PixelRect inner{panel.x + margin, panel.y + margin, std::max(0, panel.width - 2 * margin),
                        std::max(0, panel.height - 2 * margin)};
  if (inner.width <= 0 || inner.height <= 0)
    return regions;
  if (!map) {
    regions.text = inner;
    return regions;
  }
  if (side == PanelSide::top || side == PanelSide::bottom) {
    const auto size = text ? std::min(inner.height, inner.width / 2) : inner.height;
    regions.map = PixelRect{inner.x, inner.y, size, size};
    if (text && inner.width - size - margin > 0) {
      regions.text =
          PixelRect{inner.x + size + margin, inner.y, inner.width - size - margin, inner.height};
    }
    return regions;
  }
  const auto size = text ? std::min(inner.width, static_cast<int>(std::lround(inner.height * 0.6)))
                         : std::min(inner.width, inner.height);
  regions.map = PixelRect{inner.x, inner.y, size, size};
  if (text && inner.height - size - margin > 0) {
    regions.text =
        PixelRect{inner.x, inner.y + size + margin, inner.width, inner.height - size - margin};
  }
  return regions;
}

std::vector<int> width_presets(const AspectRatio frame) {
  // Heights people upload: 720p, 1080p, 1440p, 2160p; the width follows.
  std::vector<int> widths;
  for (const auto height : {720, 1080, 1440, 2160}) {
    const auto width = even(height * frame.value());
    if (width >= 16 && width <= 16384)
      widths.push_back(width);
  }
  return widths;
}

double crop_aspect(const double canvas_aspect, const Rect &crop) {
  return crop.width * canvas_aspect / crop.height;
}

bool crop_fits(const double canvas_aspect, const Rect &crop, const double picture,
               const double tolerance) {
  constexpr double kEdge = 1e-6;
  if (!(crop.width > 0.0 && crop.height > 0.0 && crop.x >= -kEdge && crop.y >= -kEdge &&
        crop.x + crop.width <= 1.0 + kEdge && crop.y + crop.height <= 1.0 + kEdge &&
        canvas_aspect > 0.0 && picture > 0.0)) {
    return false;
  }
  return std::abs(crop_aspect(canvas_aspect, crop) / picture - 1.0) <= tolerance;
}

Rect fit_crop(const double canvas_aspect, const double picture, const double centre_x,
              const double centre_y, const double height) {
  // In canvas units a crop of picture aspect p has width = height · p / canvas.
  const auto ratio = picture / canvas_aspect;
  const auto largest_height = std::min(1.0, 1.0 / ratio);
  Rect crop;
  crop.height = std::clamp(height, 1e-4, largest_height);
  crop.width = crop.height * ratio;
  crop.x = std::clamp(centre_x - crop.width / 2.0, 0.0, 1.0 - crop.width);
  crop.y = std::clamp(centre_y - crop.height / 2.0, 0.0, 1.0 - crop.height);
  return crop;
}

Rect move_crop(const Rect &crop, const double dx, const double dy) {
  Rect moved = crop;
  moved.x = std::clamp(crop.x + dx, 0.0, std::max(0.0, 1.0 - crop.width));
  moved.y = std::clamp(crop.y + dy, 0.0, std::max(0.0, 1.0 - crop.height));
  return moved;
}

Rect scale_crop(const Rect &crop, const double factor, const double fx, const double fy,
                const double minimum_height) {
  const auto ratio = crop.width / crop.height;
  const auto largest_height = std::min(1.0, 1.0 / ratio);
  const auto height =
      std::clamp(crop.height * factor, std::min(minimum_height, largest_height), largest_height);
  const auto applied = height / crop.height;
  Rect scaled;
  scaled.height = height;
  scaled.width = height * ratio;
  scaled.x = fx + (crop.x - fx) * applied;
  scaled.y = fy + (crop.y - fy) * applied;
  return move_crop(scaled, 0.0, 0.0);
}

std::string view_mode_name(const int mode) {
  if (mode < 0 || mode >= static_cast<int>(kViewNames.size()))
    return "rectilinear";
  return std::string(kViewNames[static_cast<std::size_t>(mode)]);
}

std::optional<int> parse_view_mode(const std::string &name) {
  const auto found = std::find(kViewNames.begin(), kViewNames.end(), name);
  if (found == kViewNames.end())
    return std::nullopt;
  return static_cast<int>(std::distance(kViewNames.begin(), found));
}

bool is_spherical_mode(const int mode) { return mode >= 0 && mode <= 3; }

std::optional<double> output_scale(const ResolutionInput &input) {
  if (input.source_width <= 0 || input.source_height <= 0 || input.picture_width <= 0 ||
      input.picture_height <= 0 || !(input.crop.width > 0.0) || !(input.crop.height > 0.0) ||
      !(input.canvas_aspect > 0.0) || !(input.zoom > 0.0)) {
    return std::nullopt;
  }
  // The whole canvas at output resolution.
  const auto canvas_width = input.picture_width / input.crop.width;
  const auto canvas_height = input.picture_height / input.crop.height;
  const auto mode = input.source == SourceKind::flat ? 7 : input.mode;

  if (mode >= 5) {
    const auto content = static_cast<double>(input.source_width) / input.source_height;
    const auto shown_width =
        (input.canvas_aspect > content ? canvas_height * content : canvas_width) * input.zoom;
    return shown_width / input.source_width;
  }

  double source_per_radian{};
  if (input.source == SourceKind::max_dual_eac) {
    // Each cube face covers 90° across the stream height.
    source_per_radian = input.source_height / (std::numbers::pi / 2.0);
  } else {
    if (!(input.panorama_crop_width > 0.0))
      return std::nullopt;
    source_per_radian = input.source_width / (input.panorama_crop_width * 2.0 * std::numbers::pi);
  }

  const auto fov = radians(std::clamp(input.field_of_view_degrees, 1.0, 359.0));
  const auto half = std::max(canvas_width, canvas_height) / 2.0;
  double output_per_radian{};
  switch (mode) {
  case 0: // rectilinear: the vertical field of view spans the canvas height
    output_per_radian = (canvas_height / 2.0) / std::tan(fov / 2.0);
    break;
  case 1: // fisheye: equal angle, each axis normalised separately
    output_per_radian = half / (fov / 2.0);
    break;
  case 2: // stereographic and little planet: r = tan(θ/2) / tan(fov/4)
  case 3:
    output_per_radian = half / (2.0 * std::tan(fov / 4.0));
    break;
  case 4: { // panorama: 360° across the fitted 2:1 picture
    const auto shown_width =
        (input.canvas_aspect > 2.0 ? canvas_height * 2.0 : canvas_width) * input.zoom;
    output_per_radian = shown_width / (2.0 * std::numbers::pi);
    break;
  }
  default:
    return std::nullopt;
  }
  return output_per_radian / source_per_radian;
}

std::string still_file_name(const std::string &stem, const std::optional<std::string> &position,
                            const std::string &view, const std::string &extension) {
  auto name = stem;
  if (position)
    name += "_" + *position;
  name += "_" + view + "." + extension;
  return name;
}

} // namespace gb::exporting
