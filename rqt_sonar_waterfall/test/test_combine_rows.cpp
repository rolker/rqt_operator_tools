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

#include <optional>
#include <utility>
#include <vector>

#include "rqt_sonar_waterfall/waterfall_model.hpp"

namespace
{

using rqt_sonar_waterfall::WaterfallRow;
using rqt_sonar_waterfall::combine_rows;

WaterfallRow row(std::vector<float> v, double range, double stamp)
{
  WaterfallRow r;
  r.intensities = std::move(v);
  r.range_max = range;
  r.stamp = stamp;
  return r;
}

}  // namespace

TEST(CombineRows, BothSidesPortReversedThenStarboard)
{
  // Port near->far {1,2,3}; starboard near->far {4,5,6}.
  // Result runs port-far -> nadir -> starboard-far: {3,2,1,4,5,6}.
  auto out = combine_rows(
    row({1, 2, 3}, 30.0, 10.0),
    row({4, 5, 6}, 25.0, 12.0));
  ASSERT_TRUE(out.has_value());
  EXPECT_EQ(out->intensities, (std::vector<float>{3, 2, 1, 4, 5, 6}));
  EXPECT_DOUBLE_EQ(out->range_max, 30.0);  // larger of the two
  EXPECT_DOUBLE_EQ(out->stamp, 12.0);      // later of the two
  EXPECT_EQ(out->nadir_index, 3u);         // split = port sample count
  EXPECT_DOUBLE_EQ(out->range_max_port, 30.0);
  EXPECT_DOUBLE_EQ(out->range_max_stbd, 25.0);
}

TEST(CombineRows, DifferingWidths)
{
  auto out = combine_rows(row({1, 2}, 0.0, 0.0), row({7, 8, 9}, 0.0, 0.0));
  ASSERT_TRUE(out.has_value());
  EXPECT_EQ(out->intensities, (std::vector<float>{2, 1, 7, 8, 9}));
  EXPECT_EQ(out->nadir_index, 2u);
}

TEST(CombineRows, PortOnlyReversedAllLeftOfNadir)
{
  // Single side is laid out the same way as a pair so nadir stays centered:
  // port is reversed (far->nadir) and nadir_index = full size (all left).
  auto out = combine_rows(row({1, 2, 3}, 30.0, 5.0), std::nullopt);
  ASSERT_TRUE(out.has_value());
  EXPECT_EQ(out->intensities, (std::vector<float>{3, 2, 1}));
  EXPECT_EQ(out->nadir_index, 3u);
  EXPECT_DOUBLE_EQ(out->range_max, 30.0);
  EXPECT_DOUBLE_EQ(out->range_max_port, 30.0);
  EXPECT_DOUBLE_EQ(out->range_max_stbd, 0.0);
}

TEST(CombineRows, StarboardOnlyAllRightOfNadir)
{
  auto out = combine_rows(std::nullopt, row({4, 5}, 20.0, 7.0));
  ASSERT_TRUE(out.has_value());
  EXPECT_EQ(out->intensities, (std::vector<float>{4, 5}));
  EXPECT_EQ(out->nadir_index, 0u);  // all right of nadir
  EXPECT_DOUBLE_EQ(out->range_max_stbd, 20.0);
  EXPECT_DOUBLE_EQ(out->range_max_port, 0.0);
}

TEST(CombineRows, NeitherReturnsNullopt)
{
  EXPECT_FALSE(combine_rows(std::nullopt, std::nullopt).has_value());
}
