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

#include <vector>

#include "rqt_sonar_waterfall/ping_pairer.hpp"

namespace
{

using rqt_sonar_waterfall::PingPairer;
using rqt_sonar_waterfall::WaterfallRow;

WaterfallRow row(std::vector<float> v)
{
  WaterfallRow r;
  r.intensities = std::move(v);
  return r;
}

}  // namespace

TEST(PingPairer, BothActiveStarboardTriggersPairedRow)
{
  PingPairer p;
  p.set_sides(true, true);
  // Port latches, emits nothing yet.
  EXPECT_FALSE(p.submit_port(row({1, 2, 3})).has_value());
  // Starboard triggers: port reversed (3,2,1) then starboard (4,5).
  auto out = p.submit_starboard(row({4, 5}));
  ASSERT_TRUE(out.has_value());
  EXPECT_EQ(out->intensities, (std::vector<float>{3, 2, 1, 4, 5}));
}

TEST(PingPairer, PortOnlyEmitsPortRow)
{
  PingPairer p;
  p.set_sides(true, false);
  auto out = p.submit_port(row({1, 2, 3}));
  ASSERT_TRUE(out.has_value());
  EXPECT_EQ(out->intensities, (std::vector<float>{1, 2, 3}));
}

TEST(PingPairer, StarboardOnlyEmitsStarboardRow)
{
  PingPairer p;
  p.set_sides(false, true);
  auto out = p.submit_starboard(row({7, 8}));
  ASSERT_TRUE(out.has_value());
  EXPECT_EQ(out->intensities, (std::vector<float>{7, 8}));
}

TEST(PingPairer, BothActiveStarboardBeforePortIsStarboardOnly)
{
  PingPairer p;
  p.set_sides(true, true);
  auto out = p.submit_starboard(row({9, 10}));
  ASSERT_TRUE(out.has_value());
  EXPECT_EQ(out->intensities, (std::vector<float>{9, 10}));  // no port yet
}

TEST(PingPairer, SetSidesClearsLatchedRows)
{
  PingPairer p;
  p.set_sides(true, true);
  p.submit_port(row({1, 2, 3}));   // latch a port
  p.set_sides(true, true);         // re-declare sides -> clears latch
  auto out = p.submit_starboard(row({4, 5}));
  ASSERT_TRUE(out.has_value());
  EXPECT_EQ(out->intensities, (std::vector<float>{4, 5}));  // stale port dropped
}

TEST(PingPairer, ReportsActiveSides)
{
  PingPairer p;
  p.set_sides(true, false);
  EXPECT_TRUE(p.port_active());
  EXPECT_FALSE(p.starboard_active());
}
