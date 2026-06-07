// Copyright 2026 University of New Hampshire
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
//    * Redistributions of source code must retain the above copyright
//      notice, this list of conditions and the following disclaimer.
//
//    * Redistributions in binary form must reproduce the above copyright
//      notice, this list of conditions and the following disclaimer in the
//      documentation and/or other materials provided with the distribution.
//
//    * Neither the name of the University of New Hampshire nor the names of its
//      contributors may be used to endorse or promote products derived from
//      this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.

#ifndef RQT_SONAR_WATERFALL__COLOR_MAP_HPP_
#define RQT_SONAR_WATERFALL__COLOR_MAP_HPP_

#include <cstdint>
#include <deque>
#include <utility>
#include <vector>

#include "rqt_sonar_waterfall/waterfall_model.hpp"

namespace marine_colormap
{
class Palette;  // full definition pulled into color_map.cpp; only a pointer here
}  // namespace marine_colormap

namespace rqt_sonar_waterfall
{

/// An 8-bit-per-channel color, independent of any GUI toolkit so the mapping
/// stays unit-testable without a QApplication.
struct Rgb
{
  uint8_t r = 0;
  uint8_t g = 0;
  uint8_t b = 0;
};

inline bool operator==(const Rgb & a, const Rgb & b)
{
  return a.r == b.r && a.g == b.g && a.b == b.b;
}

/// Built-in waterfall palettes.
enum class ColorMapType
{
  Grayscale,  ///< linear black -> white
  Bronze,     ///< black -> amber/bronze -> pale (classic sidescan look)
  Thermal,    ///< multi-hue palette adapted from rviz_sonar_image
};

/// Maps a normalized intensity in [0, 1] to an Rgb color via a built-in palette.
///
/// The palette data and interpolation are sourced from the shared
/// `marine_colormap` library (single source of truth across rqt/rviz/CAMP); this
/// is a thin adapter that exposes them through the rqt-side `Rgb`/`ColorMapType`
/// API the waterfall widget already uses.
class ColorMap
{
public:
  explicit ColorMap(ColorMapType type = ColorMapType::Grayscale);

  void set_type(ColorMapType type);
  ColorMapType type() const {return type_;}

  /// Look up the color for `t`; `t` is clamped to [0, 1].
  Rgb lookup(float t) const;

private:
  ColorMapType type_ = ColorMapType::Grayscale;
  const marine_colormap::Palette * palette_ = nullptr;
};

/// Number of built-in palettes (the selector lists them in enum order).
inline constexpr int kColorMapCount = 3;

/// Human-readable name for a palette (used as the selector label).
const char * color_map_name(ColorMapType type);

/// Selector index (0..kColorMapCount-1) for a palette.
int color_map_index(ColorMapType type);

/// Palette for a selector index; out-of-range indices clamp to Grayscale.
ColorMapType color_map_from_index(int index);

/// Map a raw sample value to a normalized display level in [0, 1].
///
/// Normalizes against [min, max], then applies `gain` (linear brightness, >0)
/// and `contrast` (gamma, >0: values >1 lift the mid-tones). A degenerate range
/// (max <= min) yields 0. The result is always clamped to [0, 1].
float scale_intensity(
  float value, float min, float max, float gain = 1.0f, float contrast = 1.0f);

/// Find the [min, max] intensity across every sample in the buffered rows.
///
/// Returns {0, 1} when there are no samples, so callers always get a usable
/// (non-degenerate) range for auto-scaling.
std::pair<float, float> auto_range(const std::deque<WaterfallRow> & rows);

}  // namespace rqt_sonar_waterfall

#endif  // RQT_SONAR_WATERFALL__COLOR_MAP_HPP_
