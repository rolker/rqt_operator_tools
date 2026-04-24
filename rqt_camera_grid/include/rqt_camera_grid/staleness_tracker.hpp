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

  void mark_frame(const rclcpp::Time & now);

  bool has_frames() const;

  Level tick(const rclcpp::Time & now) const;

private:
  double warn_s_;
  double error_s_;
  rclcpp::Time last_frame_;
  bool has_frames_ = false;
};

}  // namespace rqt_camera_grid

#endif  // RQT_CAMERA_GRID__STALENESS_TRACKER_HPP_
