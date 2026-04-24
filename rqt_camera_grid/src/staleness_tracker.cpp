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

#include "rqt_camera_grid/staleness_tracker.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace rqt_camera_grid
{

StalenessTracker::StalenessTracker(double warn_s, double error_s)
: warn_s_(std::max(0.0, warn_s)),
  error_s_(std::max(warn_s_, error_s)),
  last_arrival_(0, 0, RCL_ROS_TIME),
  last_valid_stamp_(0, 0, RCL_ROS_TIME)
{
}

void StalenessTracker::set_thresholds(double warn_s, double error_s)
{
  warn_s_ = std::max(0.0, warn_s);
  error_s_ = std::max(warn_s_, error_s);
}

void StalenessTracker::mark_frame(
  const rclcpp::Time & arrival,
  const rclcpp::Time & header_stamp,
  bool stamp_valid)
{
  last_arrival_ = arrival;
  has_frames_ = true;
  last_frame_stamp_valid_ = stamp_valid;
  if (stamp_valid) {
    last_valid_stamp_ = header_stamp;
    has_valid_stamp_ = true;
  }
}

bool StalenessTracker::has_frames() const
{
  return has_frames_;
}

bool StalenessTracker::last_stamp_valid() const
{
  return last_frame_stamp_valid_;
}

StalenessTracker::Level StalenessTracker::tick(const rclcpp::Time & now) const
{
  if (!has_frames_) {
    return Level::Error;
  }
  // A stream we can't vet must not look healthy. The operator sees the
  // red border and the [no stamp] label together and knows this pane's
  // latency is unverifiable.
  if (!last_frame_stamp_valid_) {
    return Level::Error;
  }
  const double arr_age = (now - last_arrival_).seconds();
  const double hdr_age = (now - last_valid_stamp_).seconds();
  const double age = std::max(arr_age, hdr_age);
  // Negative age means time ran backwards (e.g. use_sim_time + clock
  // restart) or a publisher's stamp is slightly ahead of ours within
  // the 60 s tolerance — prefer Error so the operator sees a
  // conservative signal through the discontinuity.
  if (age < 0.0 || age >= error_s_) {
    return Level::Error;
  }
  if (age >= warn_s_) {
    return Level::Warn;
  }
  return Level::Neutral;
}

double StalenessTracker::arrival_age(const rclcpp::Time & now) const
{
  if (!has_frames_) {
    return std::numeric_limits<double>::quiet_NaN();
  }
  return (now - last_arrival_).seconds();
}

double StalenessTracker::stamp_age(const rclcpp::Time & now) const
{
  if (!has_valid_stamp_) {
    return std::numeric_limits<double>::quiet_NaN();
  }
  return (now - last_valid_stamp_).seconds();
}

}  // namespace rqt_camera_grid
