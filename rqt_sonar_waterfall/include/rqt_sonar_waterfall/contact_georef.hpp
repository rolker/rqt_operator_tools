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

#ifndef RQT_SONAR_WATERFALL__CONTACT_GEOREF_HPP_
#define RQT_SONAR_WATERFALL__CONTACT_GEOREF_HPP_

#include <string>
#include <vector>

#include <geometry_msgs/msg/transform.hpp>

#include "marine_perception_tools/contact_store.hpp"

namespace rqt_sonar_waterfall
{

/// Georeferenced footprint of a box drawn on the live sidescan waterfall.
///
/// `corners` are in a LOCAL sensor frame (x = along-track, y = athwartship,
/// metres) ready to hand to marine_perception_tools::make_box_contact(), so the
/// resulting Contact's BOX `shape.dimensions` are true athwartship x alongtrack
/// metres (NOT ECEF deltas — see georeference_box()). `latitude`/`longitude` are
/// the box centroid resolved to WGS84 degrees for the Contact's archival
/// `geo_pose`; `altitude` is its ellipsoidal height.
struct GeorefBox
{
  bool ok = false;
  std::vector<marine_perception_tools::MapPoint> corners;
  std::string frame;       ///< Contact header.frame_id (the sensor frame)
  double stamp_s = 0.0;    ///< representative ping stamp (seconds since epoch)
  double latitude = 0.0;   ///< centroid latitude, degrees
  double longitude = 0.0;  ///< centroid longitude, degrees
  double altitude = 0.0;   ///< centroid ellipsoidal height, metres
};

/// Georeference a box marked on the waterfall into a map-frame footprint plus a
/// geodetic centroid (issue #86, Plan-Review must-fix #1).
///
/// REP-105 `earth` is ECEF, while make_box_contact()'s contract takes map-frame
/// ENU metres, so this splits the two concerns instead of feeding ECEF deltas as
/// box corners (which would yield a rotated box with wrong dimensions):
///  - The BOX *extent* is built directly in the sensor's local frame from the
///    slant->ground geometry: athwartship width = |range span across track|,
///    alongtrack length = the vessel's travel between the first and last marked
///    ping. `corners` carry that extent (x = along, y = athwart).
///  - Only the *geodetic centroid* is resolved: the centroid's sensor-frame
///    offset is rotated into ECEF by the middle ping's pose, added to that
///    ping's ECEF origin, and converted ECEF->geodetic via Geocentric::Reverse.
///
/// Axis convention (Open Question #1): the sensor frame is assumed REP-103 body
/// (x forward = along-track, y left = port, z up), so a port return (negative
/// display range) maps to +y. The athwartship offset is taken as horizontal
/// ground range. Verify against the live sidescan mount before trusting the
/// across-track *side* of resolved contacts.
///
/// @param poses   earth<-sensor transforms for the marked rows, oldest-first.
/// @param stamps  matching ping stamps (same length as `poses`).
/// @param frame   sensor frame_id, used as the Contact frame.
/// @param range_left_m,range_right_m  signed across-track ground range (metres)
///        at the box's left/right edges; negative = port/left on the display.
/// @return ok=false when `poses` is empty or a pose quaternion is degenerate.
GeorefBox georeference_box(
  const std::vector<geometry_msgs::msg::Transform> & poses,
  const std::vector<double> & stamps,
  const std::string & frame,
  double range_left_m, double range_right_m);

}  // namespace rqt_sonar_waterfall

#endif  // RQT_SONAR_WATERFALL__CONTACT_GEOREF_HPP_
