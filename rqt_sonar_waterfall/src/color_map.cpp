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

#include "rqt_sonar_waterfall/color_map.hpp"

#include <algorithm>
#include <limits>

#include <marine_colormap/color.hpp>
#include <marine_colormap/palette.hpp>
#include <marine_colormap/transfer.hpp>

namespace rqt_sonar_waterfall
{

namespace
{

// Map the rqt palette enum to the shared marine_colormap palette -- the single
// source of truth for the stop data. find_palette never returns null for a
// built-in name.
const marine_colormap::Palette * shared_palette(ColorMapType type)
{
  switch (type) {
    case ColorMapType::Bronze:
      return marine_colormap::find_palette("bronze");
    case ColorMapType::Thermal:
      return marine_colormap::find_palette("thermal");
    case ColorMapType::Grayscale:
    default:
      return marine_colormap::find_palette("grayscale");
  }
}

}  // namespace

const char * color_map_name(ColorMapType type)
{
  switch (type) {
    case ColorMapType::Grayscale:
      return "Grayscale";
    case ColorMapType::Bronze:
      return "Bronze";
    case ColorMapType::Thermal:
      return "Thermal";
  }
  return "Grayscale";
}

int color_map_index(ColorMapType type)
{
  switch (type) {
    case ColorMapType::Grayscale:
      return 0;
    case ColorMapType::Bronze:
      return 1;
    case ColorMapType::Thermal:
      return 2;
  }
  return 0;
}

ColorMapType color_map_from_index(int index)
{
  switch (index) {
    case 1:
      return ColorMapType::Bronze;
    case 2:
      return ColorMapType::Thermal;
    default:
      return ColorMapType::Grayscale;
  }
}

ColorMap::ColorMap(ColorMapType type)
{
  set_type(type);
}

void ColorMap::set_type(ColorMapType type)
{
  type_ = type;
  palette_ = shared_palette(type);
}

Rgb ColorMap::lookup(float t) const
{
  // palette_ is set from a built-in name via shared_palette() and never null in
  // practice; guard defensively so a lib change can't crash the render path.
  if (palette_ == nullptr) {
    return Rgb{0, 0, 0};
  }
  // sample() clamps t to [0, 1] and interpolates; to_rgba8 quantizes. Drop the
  // (always-opaque) alpha to the rqt RGB888 pixel type.
  const marine_colormap::Rgba8 c = marine_colormap::to_rgba8(palette_->sample(t));
  return Rgb{c.r, c.g, c.b};
}

float scale_intensity(float value, float min, float max, float gain, float contrast)
{
  // Identical pipeline to marine_colormap (normalize -> gain -> contrast),
  // sourced from the lib so the rqt CPU path matches its CPU/GPU formula exactly.
  return marine_colormap::apply_response(
    marine_colormap::normalize(value, min, max), gain, contrast);
}

std::pair<float, float> auto_range(const std::deque<WaterfallRow> & rows)
{
  float lo = std::numeric_limits<float>::max();
  float hi = std::numeric_limits<float>::lowest();
  bool any = false;
  for (const auto & row : rows) {
    if (row.has_intensity_range) {
      // Per-row extremes cached at buffer entry: two numbers, no per-sample scan.
      lo = std::min(lo, row.min_intensity);
      hi = std::max(hi, row.max_intensity);
      any = true;
    } else {
      // Fallback for rows constructed directly (e.g. unit tests).
      for (float v : row.intensities) {
        lo = std::min(lo, v);
        hi = std::max(hi, v);
        any = true;
      }
    }
  }
  if (!any) {
    return {0.0f, 1.0f};
  }
  return {lo, hi};
}

}  // namespace rqt_sonar_waterfall
