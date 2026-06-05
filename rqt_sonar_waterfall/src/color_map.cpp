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
#include <cmath>
#include <limits>

namespace rqt_sonar_waterfall
{

namespace
{

std::vector<Rgb> palette(ColorMapType type)
{
  switch (type) {
    case ColorMapType::Grayscale:
      return {{0, 0, 0}, {255, 255, 255}};
    case ColorMapType::Bronze:
      return {
        {0, 0, 0}, {60, 30, 10}, {130, 75, 25}, {200, 140, 70}, {255, 225, 170}};
    case ColorMapType::Thermal:
    default:
      // Adapted from rviz_sonar_image::ColorMap (float channels scaled to 8-bit).
      return {
        {77, 77, 77}, {5, 102, 242}, {33, 23, 181}, {38, 166, 138},
        {18, 156, 105}, {161, 209, 61}, {252, 179, 46}, {250, 94, 153},
        {252, 48, 97}, {219, 41, 51}, {166, 51, 51}, {153, 10, 15}};
  }
}

uint8_t lerp_channel(uint8_t a, uint8_t b, float t)
{
  return static_cast<uint8_t>(std::lround(a + (b - a) * t));
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
  stops_ = palette(type);
}

Rgb ColorMap::lookup(float t) const
{
  t = std::clamp(t, 0.0f, 1.0f);
  if (stops_.size() == 1) {
    return stops_.front();
  }
  const float p = t * static_cast<float>(stops_.size() - 1);
  auto i = static_cast<std::size_t>(std::floor(p));
  if (i >= stops_.size() - 1) {
    return stops_.back();
  }
  const float frac = p - static_cast<float>(i);
  const Rgb & a = stops_[i];
  const Rgb & b = stops_[i + 1];
  return Rgb{
    lerp_channel(a.r, b.r, frac),
    lerp_channel(a.g, b.g, frac),
    lerp_channel(a.b, b.b, frac)};
}

float scale_intensity(float value, float min, float max, float gain, float contrast)
{
  if (max <= min) {
    return 0.0f;
  }
  float n = (value - min) / (max - min);
  n = std::clamp(n, 0.0f, 1.0f);
  n = std::clamp(n * gain, 0.0f, 1.0f);
  if (contrast > 0.0f && contrast != 1.0f) {
    n = std::pow(n, 1.0f / contrast);
  }
  return std::clamp(n, 0.0f, 1.0f);
}

std::pair<float, float> auto_range(const std::deque<WaterfallRow> & rows)
{
  float lo = std::numeric_limits<float>::max();
  float hi = std::numeric_limits<float>::lowest();
  bool any = false;
  for (const auto & row : rows) {
    for (float v : row.intensities) {
      lo = std::min(lo, v);
      hi = std::max(hi, v);
      any = true;
    }
  }
  if (!any) {
    return {0.0f, 1.0f};
  }
  return {lo, hi};
}

}  // namespace rqt_sonar_waterfall
