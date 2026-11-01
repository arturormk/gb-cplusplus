#pragma once

#include <optional>
#include <string>
#include <vector>

// Framing geometry for stills and exports (docs/plans/export/capture.md, settings.md). The canvas
// is the view as gb-player draws it, with its own aspect ratio; the crop is the part of it that is
// exported, in canvas units (0..1 on each axis). The frame is the whole output picture, including
// a side panel when one is set; the picture area is what the panel leaves.
namespace gb::exporting {

struct AspectRatio {
  int width{16};
  int height{9};
  [[nodiscard]] double value() const { return static_cast<double>(width) / height; }
  [[nodiscard]] std::string text() const;
  friend bool operator==(const AspectRatio &, const AspectRatio &) = default;
};

// "16:9", "4:3", "1:1", "9:16" or any "W:H" with whole numbers from 1 to 10000 and a ratio
// between 1:10 and 10:1.
[[nodiscard]] std::optional<AspectRatio> parse_aspect(const std::string &text);

enum class PanelSide { none, left, right, top, bottom };

[[nodiscard]] std::string panel_side_name(PanelSide side);
[[nodiscard]] std::optional<PanelSide> parse_panel_side(const std::string &text);

// Panel sizes are fractions of the frame height.
constexpr double kMinimumPanelSize = 0.05;
constexpr double kMaximumPanelSize = 0.9;

struct Rect {
  double x{};
  double y{};
  double width{1.0};
  double height{1.0};
  friend bool operator==(const Rect &, const Rect &) = default;
};

struct PixelRect {
  int x{};
  int y{};
  int width{};
  int height{};
  friend bool operator==(const PixelRect &, const PixelRect &) = default;
};

// The aspect ratio of the picture area: the frame's, less a panel of `panel_size` frame heights.
// Returns nothing when the panel leaves no usable picture.
[[nodiscard]] std::optional<double> picture_aspect(AspectRatio frame, PanelSide panel,
                                                   double panel_size);

struct FrameLayout {
  int width{};
  int height{};
  PixelRect picture;
  std::optional<PixelRect> panel;
};

// The output frame for a frame width: the height follows from the aspect ratio, and both are
// rounded to even numbers, as video encoders need. The panel is rounded to whole pixels and the
// picture area kept even. Throws std::invalid_argument for a width outside 16..16384 or a panel
// that leaves no picture.
[[nodiscard]] FrameLayout frame_layout(AspectRatio frame, int width, PanelSide panel = {},
                                       double panel_size = 0.0);

// Where a side panel's contents go: a square map, the telemetry text and up to two graphs, inside
// a margin of 24/1080 of the frame height, with that gap between them. A left or right panel
// stacks them top to bottom: map, text, graphs; a top or bottom panel puts them left to right.
// The graphs share the height of their region, one above the other. What is missing leaves its
// room to the rest: the map alone fills what it can; with one other part, it takes up to 60 % of
// a side panel's height (half a top panel's width), with both, 45 % (a third); the text and the
// graphs split what the map leaves 40 : 60.
struct PanelRegions {
  std::optional<PixelRect> map;
  std::optional<PixelRect> text;
  std::vector<PixelRect> graphs; // as many as asked for, unless there is no room
};
[[nodiscard]] PanelRegions panel_regions(const FrameLayout &layout, PanelSide side, bool map,
                                         bool text, int graphs = 0);

// Common frame widths for an aspect ratio, smallest first.
[[nodiscard]] std::vector<int> width_presets(AspectRatio frame);

// The crop's aspect ratio in canvas pixels, w · canvas / h.
[[nodiscard]] double crop_aspect(double canvas_aspect, const Rect &crop);
// True when the crop lies inside the canvas and has the picture's aspect ratio within the
// relative tolerance (0.1 % by default, as settings.md requires).
[[nodiscard]] bool crop_fits(double canvas_aspect, const Rect &crop, double picture_aspect,
                             double tolerance = 0.001);

// The crop of the picture's aspect ratio, centred at (centre_x, centre_y), whose height is
// `height` canvas heights, shrunk to fit and then moved inside the canvas. A height of 1 or more
// gives the largest crop that fits.
[[nodiscard]] Rect fit_crop(double canvas_aspect, double picture_aspect, double centre_x,
                            double centre_y, double height);

// Moves a crop by (dx, dy) canvas units, keeping it inside the canvas.
[[nodiscard]] Rect move_crop(const Rect &crop, double dx, double dy);

// Scales a crop about a fixed point (fx, fy), in canvas units, by `factor`, keeping its aspect
// ratio, at least `minimum_height` tall and inside the canvas.
[[nodiscard]] Rect scale_crop(const Rect &crop, double factor, double fx, double fy,
                              double minimum_height);

// The view names gb-player and gb-render share, by gb-player's mode number (VideoView::Mode):
// rectilinear, fisheye, stereographic, little-planet, panorama, raw-1, raw-2, original.
[[nodiscard]] std::string view_mode_name(int mode);
[[nodiscard]] std::optional<int> parse_view_mode(const std::string &name);
[[nodiscard]] bool is_spherical_mode(int mode); // rectilinear to little planet

// How the source is laid out, for the resolution estimate.
enum class SourceKind { flat, max_dual_eac, equirectangular };

struct ResolutionInput {
  SourceKind source{SourceKind::flat};
  int source_width{};              // first stream's luma width, pixels
  int source_height{};             // and height
  double panorama_crop_width{1.0}; // equirectangular: fraction of 360° the image covers
  int mode{};                      // VideoView::Mode
  double field_of_view_degrees{90.0};
  double zoom{1.0};
  double canvas_aspect{16.0 / 9.0};
  Rect crop;
  int picture_width{}; // output picture area, pixels
  int picture_height{};
};

// Output pixels per source pixel at the centre of the view, along the axis where output is
// densest. Above 1 the output has more pixels than the source can resolve there. An estimate:
// the projections stretch the source differently across the view. Returns nothing when the input
// is incomplete.
[[nodiscard]] std::optional<double> output_scale(const ResolutionInput &input);

// A file name for a still: "<stem>_<position>_<view>.<extension>", without the position for a
// photo, e.g. "GH010073_c2-05m31s400_rectilinear.jpg".
[[nodiscard]] std::string still_file_name(const std::string &stem,
                                          const std::optional<std::string> &position,
                                          const std::string &view, const std::string &extension);

} // namespace gb::exporting
