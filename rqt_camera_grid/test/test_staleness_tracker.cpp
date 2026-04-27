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

#include <cmath>

#include "rqt_camera_grid/staleness_tracker.hpp"

using rqt_camera_grid::StalenessTracker;

namespace
{
rclcpp::Time t(double seconds)
{
  return rclcpp::Time(static_cast<int64_t>(seconds * 1e9), RCL_ROS_TIME);
}

// Convenience: arrival and header stamp are the same, stamp is valid.
// Matches the "healthy stream" baseline used by the pre-existing tests,
// which only exercised the arrival-age path.
void mark_synced(StalenessTracker & s, double arrival_and_stamp)
{
  s.mark_frame(t(arrival_and_stamp), t(arrival_and_stamp), /*stamp_valid=*/true);
}
}  // namespace

TEST(StalenessTracker, StartsInErrorBeforeAnyFrame)
{
  StalenessTracker s(2.0, 5.0);
  EXPECT_FALSE(s.has_frames());
  EXPECT_FALSE(s.last_stamp_valid());
  EXPECT_EQ(s.tick(t(0.0)), StalenessTracker::Level::Error);
  EXPECT_EQ(s.tick(t(100.0)), StalenessTracker::Level::Error);
}

TEST(StalenessTracker, NeutralRightAfterFrame)
{
  StalenessTracker s(2.0, 5.0);
  mark_synced(s, 10.0);
  EXPECT_TRUE(s.has_frames());
  EXPECT_TRUE(s.last_stamp_valid());
  EXPECT_EQ(s.tick(t(10.0)), StalenessTracker::Level::Neutral);
  EXPECT_EQ(s.tick(t(11.0)), StalenessTracker::Level::Neutral);
  EXPECT_EQ(s.tick(t(11.999)), StalenessTracker::Level::Neutral);
}

TEST(StalenessTracker, TransitionsToWarnAtThreshold)
{
  StalenessTracker s(2.0, 5.0);
  mark_synced(s, 10.0);
  EXPECT_EQ(s.tick(t(12.0)), StalenessTracker::Level::Warn);
  EXPECT_EQ(s.tick(t(13.0)), StalenessTracker::Level::Warn);
  EXPECT_EQ(s.tick(t(14.999)), StalenessTracker::Level::Warn);
}

TEST(StalenessTracker, TransitionsToErrorAtThreshold)
{
  StalenessTracker s(2.0, 5.0);
  mark_synced(s, 10.0);
  EXPECT_EQ(s.tick(t(15.0)), StalenessTracker::Level::Error);
  EXPECT_EQ(s.tick(t(30.0)), StalenessTracker::Level::Error);
}

TEST(StalenessTracker, RecoversToNeutralOnNewFrame)
{
  StalenessTracker s(2.0, 5.0);
  mark_synced(s, 10.0);
  EXPECT_EQ(s.tick(t(20.0)), StalenessTracker::Level::Error);
  mark_synced(s, 20.5);
  EXPECT_EQ(s.tick(t(20.5)), StalenessTracker::Level::Neutral);
}

TEST(StalenessTracker, SetThresholdsRetroactively)
{
  StalenessTracker s(2.0, 5.0);
  mark_synced(s, 10.0);
  EXPECT_EQ(s.tick(t(12.5)), StalenessTracker::Level::Warn);
  s.set_thresholds(3.0, 6.0);
  EXPECT_EQ(s.tick(t(12.5)), StalenessTracker::Level::Neutral);
  EXPECT_EQ(s.tick(t(13.5)), StalenessTracker::Level::Warn);
}

TEST(StalenessTracker, ErrorClampedAboveWarn)
{
  StalenessTracker s(5.0, 2.0);  // nonsense: error < warn
  mark_synced(s, 0.0);
  // Constructor clamps error up to warn, so both fire at age >= 5s.
  EXPECT_EQ(s.tick(t(4.999)), StalenessTracker::Level::Neutral);
  EXPECT_EQ(s.tick(t(5.0)), StalenessTracker::Level::Error);
}

TEST(StalenessTracker, NegativeWarnClampedToZero)
{
  StalenessTracker s(-1.0, 5.0);
  mark_synced(s, 0.0);
  // warn_s=0 means Warn fires immediately at age 0.
  EXPECT_EQ(s.tick(t(0.0)), StalenessTracker::Level::Warn);
  EXPECT_EQ(s.tick(t(4.999)), StalenessTracker::Level::Warn);
  EXPECT_EQ(s.tick(t(5.0)), StalenessTracker::Level::Error);
}

TEST(StalenessTracker, BackwardTimeReportsError)
{
  // Simulates use_sim_time clock restart: a frame was marked at t=10s in the
  // old clock domain, then the clock rewinds so tick() sees an earlier time.
  // Negative worst-of-both age must report Error, not mask staleness as
  // Neutral.
  StalenessTracker s(2.0, 5.0);
  mark_synced(s, 10.0);
  EXPECT_EQ(s.tick(t(10.0)), StalenessTracker::Level::Neutral);
  EXPECT_EQ(s.tick(t(9.999)), StalenessTracker::Level::Error);
  EXPECT_EQ(s.tick(t(5.0)), StalenessTracker::Level::Error);
  EXPECT_EQ(s.tick(t(0.0)), StalenessTracker::Level::Error);
  // A fresh frame in the new clock domain clears the error.
  mark_synced(s, 0.0);
  EXPECT_EQ(s.tick(t(0.0)), StalenessTracker::Level::Neutral);
}

// --- Worst-of-both behavior -------------------------------------------------

TEST(StalenessTracker, BufferedLateArrivalDrivesLevelFromStampAge)
{
  // This is the failure mode the fix is actually about: the pane keeps
  // receiving frames (arrival_age stays tiny) but the stamps on those
  // frames are 25 s old (buffered somewhere upstream). The border must
  // flip to Error based on header age alone.
  StalenessTracker s(2.0, 5.0);
  const double arrival = 100.0;
  const double stamp = 75.0;  // 25 s behind
  s.mark_frame(t(arrival), t(stamp), /*stamp_valid=*/true);
  // now = arrival → arrival_age = 0, stamp_age = 25 → Error.
  EXPECT_EQ(s.tick(t(arrival)), StalenessTracker::Level::Error);
}

TEST(StalenessTracker, StuckPublisherCaughtByStampAge)
{
  // Publisher keeps republishing the same stamped frame: every arrival is
  // "fresh" locally but the stamp never advances. Worst-of-both must
  // progress through Warn → Error based on the frozen stamp.
  StalenessTracker s(2.0, 5.0);
  const double frozen_stamp = 10.0;
  // Simulate three arrivals 1 s apart, all with the same old stamp.
  s.mark_frame(t(11.0), t(frozen_stamp), true);
  EXPECT_EQ(s.tick(t(11.0)), StalenessTracker::Level::Neutral);  // stamp_age=1
  s.mark_frame(t(12.5), t(frozen_stamp), true);
  EXPECT_EQ(s.tick(t(12.5)), StalenessTracker::Level::Warn);     // stamp_age=2.5
  s.mark_frame(t(15.5), t(frozen_stamp), true);
  EXPECT_EQ(s.tick(t(15.5)), StalenessTracker::Level::Error);    // stamp_age=5.5
}

TEST(StalenessTracker, TransportDropDrivesLevelFromArrivalAge)
{
  // Arrival-only failure: a frame arrived fresh, then transport dropped.
  // No new frames arrive; arrival_age climbs alongside stamp_age at the
  // same rate (both measured from the same moment-of-last-frame).
  StalenessTracker s(2.0, 5.0);
  s.mark_frame(t(10.0), t(10.0), /*stamp_valid=*/true);
  EXPECT_EQ(s.tick(t(12.5)), StalenessTracker::Level::Warn);
  EXPECT_EQ(s.tick(t(15.5)), StalenessTracker::Level::Error);
}

TEST(StalenessTracker, WorstOfBothTakesMaxAge)
{
  // A small positive skew between clocks (stamp 0.3 s behind arrival)
  // should not flip the border when neither age individually exceeds
  // the threshold, but stamp_age should drive the transition once it
  // passes warn.
  StalenessTracker s(2.0, 5.0);
  s.mark_frame(t(10.0), t(9.7), /*stamp_valid=*/true);
  // arrival_age=0, stamp_age=0.3 → Neutral.
  EXPECT_EQ(s.tick(t(10.0)), StalenessTracker::Level::Neutral);
  // arrival_age=1.9, stamp_age=2.2 → Warn (stamp drives).
  EXPECT_EQ(s.tick(t(11.9)), StalenessTracker::Level::Warn);
  // arrival_age=4.9, stamp_age=5.2 → Error (stamp drives).
  EXPECT_EQ(s.tick(t(14.9)), StalenessTracker::Level::Error);
}

// --- Invalid-stamp handling -------------------------------------------------

TEST(StalenessTracker, InvalidStampForcesError)
{
  // A frame with an invalid stamp (e.g. uninitialized zero) must push the
  // tracker into Error regardless of how fresh the arrival is — silent
  // fallback to arrival-only is the whole bug we're fixing.
  StalenessTracker s(2.0, 5.0);
  s.mark_frame(t(100.0), t(0.0), /*stamp_valid=*/false);
  EXPECT_TRUE(s.has_frames());
  EXPECT_FALSE(s.last_stamp_valid());
  EXPECT_EQ(s.tick(t(100.0)), StalenessTracker::Level::Error);
}

TEST(StalenessTracker, InvalidStampAfterHealthyFramesStillForcesError)
{
  // Pane has been healthy for a while, then one frame arrives with a
  // broken stamp. Behavior should reflect the current stream state: we
  // can no longer vet latency, so the border flips to Error.
  StalenessTracker s(2.0, 5.0);
  s.mark_frame(t(10.0), t(10.0), /*stamp_valid=*/true);
  EXPECT_EQ(s.tick(t(10.0)), StalenessTracker::Level::Neutral);
  s.mark_frame(t(10.1), t(0.0), /*stamp_valid=*/false);
  EXPECT_FALSE(s.last_stamp_valid());
  EXPECT_EQ(s.tick(t(10.1)), StalenessTracker::Level::Error);
}

TEST(StalenessTracker, ValidStampAfterInvalidRecovers)
{
  // Operator restarts the broken publisher; a fresh frame with a valid
  // stamp should clear the Error state.
  StalenessTracker s(2.0, 5.0);
  s.mark_frame(t(10.0), t(0.0), /*stamp_valid=*/false);
  EXPECT_EQ(s.tick(t(10.0)), StalenessTracker::Level::Error);
  s.mark_frame(t(10.5), t(10.5), /*stamp_valid=*/true);
  EXPECT_TRUE(s.last_stamp_valid());
  EXPECT_EQ(s.tick(t(10.5)), StalenessTracker::Level::Neutral);
}

// --- Age accessors ----------------------------------------------------------

TEST(StalenessTracker, AgeAccessorsBeforeAnyFrameReturnNaN)
{
  StalenessTracker s(2.0, 5.0);
  EXPECT_TRUE(std::isnan(s.arrival_age(t(0.0))));
  EXPECT_TRUE(std::isnan(s.stamp_age(t(0.0))));
}

TEST(StalenessTracker, AgeAccessorsReflectIndividualAges)
{
  StalenessTracker s(2.0, 5.0);
  s.mark_frame(t(100.0), t(75.0), /*stamp_valid=*/true);
  EXPECT_DOUBLE_EQ(s.arrival_age(t(100.0)), 0.0);
  EXPECT_DOUBLE_EQ(s.stamp_age(t(100.0)), 25.0);
  EXPECT_DOUBLE_EQ(s.arrival_age(t(101.0)), 1.0);
  EXPECT_DOUBLE_EQ(s.stamp_age(t(101.0)), 26.0);
}

TEST(StalenessTracker, StampAgeNanWhenLatestFrameHasInvalidStampAndNoPriorValid)
{
  StalenessTracker s(2.0, 5.0);
  s.mark_frame(t(10.0), t(0.0), /*stamp_valid=*/false);
  EXPECT_FALSE(std::isnan(s.arrival_age(t(10.0))));
  EXPECT_TRUE(std::isnan(s.stamp_age(t(10.0))));
}
