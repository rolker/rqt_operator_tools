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

#include <cstddef>
#include <vector>

#include "rqt_sonar_waterfall/waterfall_model.hpp"

namespace
{

using rqt_sonar_waterfall::apply_tvg;
using rqt_sonar_waterfall::ground_range;
using rqt_sonar_waterfall::project_row;

// value == index, so a projected column's value reveals which sample it pulled.
std::vector<float> ramp(std::size_t n)
{
  std::vector<float> v(n);
  for (std::size_t i = 0; i < n; ++i) {
    v[i] = static_cast<float>(i);
  }
  return v;
}

}  // namespace

// --- ground_range -----------------------------------------------------------

TEST(GroundRange, ZeroAltitudeIsSlant)
{
  EXPECT_DOUBLE_EQ(ground_range(10.0, 0.0), 10.0);
}

TEST(GroundRange, Pythagorean)
{
  EXPECT_DOUBLE_EQ(ground_range(5.0, 3.0), 4.0);
}

TEST(GroundRange, AltitudeDeeperThanRangeClampsToZero)
{
  EXPECT_DOUBLE_EQ(ground_range(3.0, 5.0), 0.0);
}

// --- project_row ------------------------------------------------------------

TEST(ProjectRow, StarboardOnlyNadirCenteredLeftHalfBlank)
{
  // 4 starboard samples {10,20,30,40}, slant axis 0..40, no port side.
  const std::vector<float> s{10, 20, 30, 40};
  auto out = project_row(s, /*nadir_index=*/0, /*range_port=*/0.0,
    /*range_stbd=*/40.0, /*altitude=*/0.0, /*ground=*/false,
    /*half_width=*/40.0, /*columns=*/9);
  ASSERT_EQ(out.size(), 9u);
  // Left half (port absent) is black; nadir at the center column = sample 0.
  for (std::size_t x = 0; x < 4; ++x) {
    EXPECT_FLOAT_EQ(out[x], 0.0f) << "left column " << x;
  }
  EXPECT_FLOAT_EQ(out[4], 10.0f);  // nadir
  EXPECT_FLOAT_EQ(out[8], 40.0f);  // far edge
}

TEST(ProjectRow, SymmetricSidesProduceSymmetricOutput)
{
  // Reversed-port {40,30,20,10} | starboard {10,20,30,40}, nadir at index 4.
  const std::vector<float> s{40, 30, 20, 10, 10, 20, 30, 40};
  auto out = project_row(s, /*nadir_index=*/4, 40.0, 40.0, 0.0, false, 40.0, 9);
  ASSERT_EQ(out.size(), 9u);
  for (std::size_t x = 0; x < 9; ++x) {
    EXPECT_FLOAT_EQ(out[x], out[8 - x]) << "asymmetry at column " << x;
  }
}

TEST(ProjectRow, GroundModeRemovesWaterColumnAtCenter)
{
  // 10 starboard samples, value==index, slant 0..10, altitude 6.
  // Center column (ground d=0) -> slant 6 -> sample index 5, NOT the nadir
  // sample 0: the 0..6 m water column is excised.
  const std::vector<float> s = ramp(10);
  const double half = ground_range(10.0, 6.0);  // sqrt(100-36) = 8
  ASSERT_DOUBLE_EQ(half, 8.0);
  auto out = project_row(s, 0, 0.0, 10.0, 6.0, /*ground=*/true, half, 9);
  ASSERT_EQ(out.size(), 9u);
  EXPECT_FLOAT_EQ(out[4], 5.0f);  // bottom return at nadir, water column skipped
  EXPECT_FLOAT_EQ(out[8], 9.0f);  // far edge -> slant 10 -> last sample
}

TEST(ProjectRow, SlantBeyondRangeIsBlack)
{
  // half_width wider than the side's ground reach: far columns have s>range.
  const std::vector<float> s = ramp(10);
  auto out = project_row(s, 0, 0.0, 10.0, 6.0, /*ground=*/true,
    /*half_width=*/12.0, /*columns=*/9);
  ASSERT_EQ(out.size(), 9u);
  EXPECT_FLOAT_EQ(out[8], 0.0f);  // d=12 -> slant ~13.4 > 10 -> no data
  EXPECT_GT(out[4], 0.0f);        // center still has the bottom return
}

TEST(ProjectRow, PortOnlyFillsLeftHalfNadirCentered)
{
  // Reversed-port {30,20,10} (far->nadir), all left of a centered nadir.
  const std::vector<float> s{30, 20, 10};
  auto out = project_row(s, /*nadir_index=*/3, /*range_port=*/30.0,
    /*range_stbd=*/0.0, 0.0, false, 30.0, 9);
  ASSERT_EQ(out.size(), 9u);
  EXPECT_FLOAT_EQ(out[0], 30.0f);  // far port edge
  EXPECT_FLOAT_EQ(out[4], 10.0f);  // nadir at center
  for (std::size_t x = 5; x < 9; ++x) {
    EXPECT_FLOAT_EQ(out[x], 0.0f) << "right column " << x;  // starboard absent
  }
}

TEST(ProjectRow, DegenerateInputsYieldBlack)
{
  EXPECT_TRUE(project_row({}, 0, 10.0, 10.0, 0.0, false, 10.0, 5) ==
    std::vector<float>(5, 0.0f));
  EXPECT_TRUE(project_row(ramp(4), 0, 0.0, 10.0, 0.0, false,
    /*half_width=*/0.0, 5) == std::vector<float>(5, 0.0f));
  EXPECT_TRUE(project_row(ramp(4), 0, 0.0, 10.0, 0.0, false, 10.0,
      /*columns=*/0).empty());
}

// --- apply_tvg --------------------------------------------------------------

TEST(ApplyTvg, ZeroSlopeIsIdentity)
{
  const std::vector<float> s{1, 5, 9, 4};
  EXPECT_EQ(apply_tvg(s, 0, 0.0, 10.0, /*slope=*/0.0), s);
}

TEST(ApplyTvg, FarSamplesAmplifiedMoreThanNear)
{
  // 5 starboard samples all 1.0, range 10, slope 1, ref 1 -> factor = slant.
  // Bin centers: 10*(k+0.5)/5 = {1,3,5,7,9}.
  const std::vector<float> s(5, 1.0f);
  auto out = apply_tvg(s, /*nadir_index=*/0, /*range_port=*/0.0,
    /*range_stbd=*/10.0, /*slope=*/1.0, /*ref_range=*/1.0);
  ASSERT_EQ(out.size(), 5u);
  EXPECT_FLOAT_EQ(out[0], 1.0f);  // slant 1 == ref, factor 1
  EXPECT_FLOAT_EQ(out[4], 9.0f);  // slant 9, factor 9
  EXPECT_GT(out[4], out[0]);
}

TEST(ApplyTvg, UnknownSideRangeLeavesSamplesUnchanged)
{
  // Port side range 0 (unknown): those two samples must be copied unchanged.
  const std::vector<float> s{2, 3, 4, 5};  // port [0,2) reversed, stbd [2,4)
  auto out = apply_tvg(s, /*nadir_index=*/2, /*range_port=*/0.0,
    /*range_stbd=*/10.0, /*slope=*/1.0);
  EXPECT_FLOAT_EQ(out[0], 2.0f);
  EXPECT_FLOAT_EQ(out[1], 3.0f);
  EXPECT_GT(out[3], out[2]);  // starboard still scaled with range
}
