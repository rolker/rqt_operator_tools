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

#include <gtest/gtest.h>

#include <deque>

#include "rqt_sonar_waterfall/color_map.hpp"
#include "rqt_sonar_waterfall/waterfall_model.hpp"

namespace
{

using rqt_sonar_waterfall::auto_range;
using rqt_sonar_waterfall::color_map_from_index;
using rqt_sonar_waterfall::color_map_index;
using rqt_sonar_waterfall::color_map_name;
using rqt_sonar_waterfall::ColorMap;
using rqt_sonar_waterfall::ColorMapType;
using rqt_sonar_waterfall::kColorMapCount;
using rqt_sonar_waterfall::Rgb;
using rqt_sonar_waterfall::scale_intensity;
using rqt_sonar_waterfall::WaterfallRow;

WaterfallRow row(std::vector<float> v)
{
  WaterfallRow r;
  r.intensities = std::move(v);
  return r;
}

}  // namespace

TEST(ColorMap, GrayscaleEndpointsAndMidpoint)
{
  ColorMap cm(ColorMapType::Grayscale);
  EXPECT_EQ(cm.lookup(0.0f), (Rgb{0, 0, 0}));
  EXPECT_EQ(cm.lookup(1.0f), (Rgb{255, 255, 255}));
  EXPECT_EQ(cm.lookup(0.5f), (Rgb{128, 128, 128}));
}

TEST(ColorMap, ClampsOutOfRange)
{
  ColorMap cm(ColorMapType::Grayscale);
  EXPECT_EQ(cm.lookup(-1.0f), cm.lookup(0.0f));
  EXPECT_EQ(cm.lookup(2.0f), cm.lookup(1.0f));
}

TEST(ColorMap, BronzeStopsHit)
{
  ColorMap cm(ColorMapType::Bronze);
  EXPECT_EQ(cm.lookup(0.0f), (Rgb{0, 0, 0}));
  EXPECT_EQ(cm.lookup(0.25f), (Rgb{60, 30, 10}));   // exact stop
  EXPECT_EQ(cm.lookup(1.0f), (Rgb{255, 225, 170}));
}

TEST(ColorMap, ThermalEndpoints)
{
  ColorMap cm(ColorMapType::Thermal);
  EXPECT_EQ(cm.lookup(0.0f), (Rgb{77, 77, 77}));
  EXPECT_EQ(cm.lookup(1.0f), (Rgb{153, 10, 15}));
}

TEST(ColorMap, SetTypeChangesPalette)
{
  ColorMap cm(ColorMapType::Grayscale);
  EXPECT_EQ(cm.type(), ColorMapType::Grayscale);
  cm.set_type(ColorMapType::Bronze);
  EXPECT_EQ(cm.type(), ColorMapType::Bronze);
  EXPECT_EQ(cm.lookup(1.0f), (Rgb{255, 225, 170}));
}

TEST(ColorMap, IndexRoundTrip)
{
  for (auto type :
    {ColorMapType::Grayscale, ColorMapType::Bronze, ColorMapType::Thermal})
  {
    EXPECT_EQ(color_map_from_index(color_map_index(type)), type);
  }
}

TEST(ColorMap, IndexOutOfRangeClampsToGrayscale)
{
  EXPECT_EQ(color_map_from_index(-1), ColorMapType::Grayscale);
  EXPECT_EQ(color_map_from_index(99), ColorMapType::Grayscale);
}

TEST(ColorMap, NamesAndCount)
{
  EXPECT_EQ(kColorMapCount, 3);
  EXPECT_STREQ(color_map_name(ColorMapType::Grayscale), "Grayscale");
  EXPECT_STREQ(color_map_name(ColorMapType::Bronze), "Bronze");
  EXPECT_STREQ(color_map_name(ColorMapType::Thermal), "Thermal");
}

TEST(ScaleIntensity, LinearMidpoint)
{
  EXPECT_FLOAT_EQ(scale_intensity(5.0f, 0.0f, 10.0f), 0.5f);
}

TEST(ScaleIntensity, ClampsBelowAndAbove)
{
  EXPECT_FLOAT_EQ(scale_intensity(-5.0f, 0.0f, 10.0f), 0.0f);
  EXPECT_FLOAT_EQ(scale_intensity(15.0f, 0.0f, 10.0f), 1.0f);
}

TEST(ScaleIntensity, DegenerateRangeIsZero)
{
  EXPECT_FLOAT_EQ(scale_intensity(5.0f, 10.0f, 10.0f), 0.0f);
}

TEST(ScaleIntensity, GainScalesAndClamps)
{
  EXPECT_FLOAT_EQ(scale_intensity(2.5f, 0.0f, 10.0f, 2.0f), 0.5f);
  EXPECT_FLOAT_EQ(scale_intensity(8.0f, 0.0f, 10.0f, 2.0f), 1.0f);
}

TEST(ScaleIntensity, ContrastGamma)
{
  // normalized 0.25 with gamma 2 => pow(0.25, 1/2) = 0.5
  EXPECT_FLOAT_EQ(scale_intensity(2.5f, 0.0f, 10.0f, 1.0f, 2.0f), 0.5f);
}

TEST(AutoRange, EmptyReturnsUnitRange)
{
  std::deque<WaterfallRow> rows;
  auto pr = auto_range(rows);
  EXPECT_FLOAT_EQ(pr.first, 0.0f);
  EXPECT_FLOAT_EQ(pr.second, 1.0f);
}

TEST(AutoRange, SpansAllSamples)
{
  std::deque<WaterfallRow> rows;
  rows.push_back(row({3.0f, -2.0f, 7.0f}));
  rows.push_back(row({10.0f, 1.0f}));
  auto pr = auto_range(rows);
  EXPECT_FLOAT_EQ(pr.first, -2.0f);
  EXPECT_FLOAT_EQ(pr.second, 10.0f);
}
