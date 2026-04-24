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

#ifndef RQT_CAMERA_GRID__STALENESS_TRACKER_HPP_
#define RQT_CAMERA_GRID__STALENESS_TRACKER_HPP_

#include <rclcpp/time.hpp>

namespace rqt_camera_grid
{

// Tracks two ages per stream and drives the border from the worst of them:
//   arrival_age = now - local-clock time the frame was received
//   stamp_age   = now - header.stamp of the most recent frame
// Worst-of-both catches three failure modes:
//   (a) transport drop — both ages climb
//   (b) buffered-late arrival — stamp_age >> arrival_age
//   (c) publisher stuck republishing the same stamped frame — same as (b)
// If the most recent frame's header.stamp is invalid (zero or far-future),
// tick() reports Error regardless of arrival_age: a stream we can't
// latency-check must not look healthy. Validity is decided by the caller.
class StalenessTracker
{
public:
  enum class Level
  {
    Neutral,
    Warn,
    Error
  };

  StalenessTracker(double warn_s, double error_s);

  void set_thresholds(double warn_s, double error_s);

  // Record a received frame.
  //   arrival:       local clock time at receive
  //   header_stamp:  msg->header.stamp (must share clock_type with arrival)
  //   stamp_valid:   caller's validity decision; false forces Error in tick()
  void mark_frame(
    const rclcpp::Time & arrival,
    const rclcpp::Time & header_stamp,
    bool stamp_valid);

  bool has_frames() const;

  // True iff the most recent frame arrived with a valid header.stamp.
  // When false, tick() reports Error and the pane label shows [no stamp].
  bool last_stamp_valid() const;

  // Returns the border level given `now`. Error if:
  //   - no frame ever received, or
  //   - most recent frame had invalid stamp, or
  //   - worst-of-both age >= error_s, or
  //   - worst-of-both age is negative (clock ran backwards).
  Level tick(const rclcpp::Time & now) const;

  // Age components for the label. Returns NaN when no data is available
  // (no frames / no valid stamp ever). Caller is expected to gate on
  // has_frames() / last_stamp_valid() before formatting.
  double arrival_age(const rclcpp::Time & now) const;
  double stamp_age(const rclcpp::Time & now) const;

private:
  double warn_s_;
  double error_s_;
  rclcpp::Time last_arrival_;
  rclcpp::Time last_valid_stamp_;
  bool has_frames_ = false;
  bool has_valid_stamp_ = false;      // ever observed a valid stamp
  bool last_frame_stamp_valid_ = false;  // validity of most recent frame
};

}  // namespace rqt_camera_grid

#endif  // RQT_CAMERA_GRID__STALENESS_TRACKER_HPP_
