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

#include "rqt_camera_grid/staleness_tracker.hpp"

using rqt_camera_grid::StalenessTracker;

namespace
{
rclcpp::Time t(double seconds)
{
  return rclcpp::Time(static_cast<int64_t>(seconds * 1e9), RCL_ROS_TIME);
}
}  // namespace

TEST(StalenessTracker, StartsInErrorBeforeAnyFrame)
{
  StalenessTracker s(2.0, 5.0);
  EXPECT_FALSE(s.has_frames());
  EXPECT_EQ(s.tick(t(0.0)), StalenessTracker::Level::Error);
  EXPECT_EQ(s.tick(t(100.0)), StalenessTracker::Level::Error);
}

TEST(StalenessTracker, NeutralRightAfterFrame)
{
  StalenessTracker s(2.0, 5.0);
  s.mark_frame(t(10.0));
  EXPECT_TRUE(s.has_frames());
  EXPECT_EQ(s.tick(t(10.0)), StalenessTracker::Level::Neutral);
  EXPECT_EQ(s.tick(t(11.0)), StalenessTracker::Level::Neutral);
  EXPECT_EQ(s.tick(t(11.999)), StalenessTracker::Level::Neutral);
}

TEST(StalenessTracker, TransitionsToWarnAtThreshold)
{
  StalenessTracker s(2.0, 5.0);
  s.mark_frame(t(10.0));
  EXPECT_EQ(s.tick(t(12.0)), StalenessTracker::Level::Warn);
  EXPECT_EQ(s.tick(t(13.0)), StalenessTracker::Level::Warn);
  EXPECT_EQ(s.tick(t(14.999)), StalenessTracker::Level::Warn);
}

TEST(StalenessTracker, TransitionsToErrorAtThreshold)
{
  StalenessTracker s(2.0, 5.0);
  s.mark_frame(t(10.0));
  EXPECT_EQ(s.tick(t(15.0)), StalenessTracker::Level::Error);
  EXPECT_EQ(s.tick(t(30.0)), StalenessTracker::Level::Error);
}

TEST(StalenessTracker, RecoversToNeutralOnNewFrame)
{
  StalenessTracker s(2.0, 5.0);
  s.mark_frame(t(10.0));
  EXPECT_EQ(s.tick(t(20.0)), StalenessTracker::Level::Error);
  s.mark_frame(t(20.5));
  EXPECT_EQ(s.tick(t(20.5)), StalenessTracker::Level::Neutral);
}

TEST(StalenessTracker, SetThresholdsRetroactively)
{
  StalenessTracker s(2.0, 5.0);
  s.mark_frame(t(10.0));
  EXPECT_EQ(s.tick(t(12.5)), StalenessTracker::Level::Warn);
  s.set_thresholds(3.0, 6.0);
  EXPECT_EQ(s.tick(t(12.5)), StalenessTracker::Level::Neutral);
  EXPECT_EQ(s.tick(t(13.5)), StalenessTracker::Level::Warn);
}

TEST(StalenessTracker, ErrorClampedAboveWarn)
{
  StalenessTracker s(5.0, 2.0);  // nonsense: error < warn
  s.mark_frame(t(0.0));
  // Constructor clamps error up to warn, so both fire at age >= 5s.
  EXPECT_EQ(s.tick(t(4.999)), StalenessTracker::Level::Neutral);
  EXPECT_EQ(s.tick(t(5.0)), StalenessTracker::Level::Error);
}

TEST(StalenessTracker, NegativeWarnClampedToZero)
{
  StalenessTracker s(-1.0, 5.0);
  s.mark_frame(t(0.0));
  // warn_s=0 means Warn fires immediately at age 0.
  EXPECT_EQ(s.tick(t(0.0)), StalenessTracker::Level::Warn);
  EXPECT_EQ(s.tick(t(4.999)), StalenessTracker::Level::Warn);
  EXPECT_EQ(s.tick(t(5.0)), StalenessTracker::Level::Error);
}
