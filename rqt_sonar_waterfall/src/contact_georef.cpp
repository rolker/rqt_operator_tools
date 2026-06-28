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

#include "rqt_sonar_waterfall/contact_georef.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

#include <GeographicLib/Geocentric.hpp>

namespace rqt_sonar_waterfall
{

namespace
{

// Rotate vector v by unit quaternion q (the standard v + 2w(u x v) + 2u x (u x v)
// form, u = q.xyz). q comes from a TF transform, so it is already normalized.
std::array<double, 3> rotate(
  const geometry_msgs::msg::Quaternion & q, const std::array<double, 3> & v)
{
  const double ux = q.x, uy = q.y, uz = q.z, w = q.w;
  // t = 2 * (u x v)
  const double tx = 2.0 * (uy * v[2] - uz * v[1]);
  const double ty = 2.0 * (uz * v[0] - ux * v[2]);
  const double tz = 2.0 * (ux * v[1] - uy * v[0]);
  // v' = v + w*t + (u x t)
  return {
    v[0] + w * tx + (uy * tz - uz * ty),
    v[1] + w * ty + (uz * tx - ux * tz),
    v[2] + w * tz + (ux * ty - uy * tx)};
}

}  // namespace

GeorefBox georeference_box(
  const std::vector<geometry_msgs::msg::Transform> & poses,
  const std::vector<double> & stamps,
  const std::string & frame,
  double range_left_m, double range_right_m)
{
  GeorefBox out;
  if (poses.empty()) {
    return out;  // ok = false
  }

  // Representative (middle) ping anchors the centroid and the contact stamp.
  const std::size_t mid = poses.size() / 2;
  const auto & mid_pose = poses[mid];
  const auto & q = mid_pose.rotation;
  // A degenerate (all-zero) quaternion means no usable orientation -> bail.
  if (q.x == 0.0 && q.y == 0.0 && q.z == 0.0 && q.w == 0.0) {
    return out;  // ok = false
  }

  // Alongtrack length = vessel travel between the first and last marked ping,
  // measured in ECEF (equals ground distance for the small motion of a few pings).
  const auto & t_first = poses.front().translation;
  const auto & t_last = poses.back().translation;
  const double dx = t_last.x - t_first.x;
  const double dy = t_last.y - t_first.y;
  const double dz = t_last.z - t_first.z;
  const double alongtrack = std::sqrt(dx * dx + dy * dy + dz * dz);

  // Athwartship extent + centre, directly in ground-range metres.
  const double d_lo = std::min(range_left_m, range_right_m);
  const double d_hi = std::max(range_left_m, range_right_m);
  const double d_mid = 0.5 * (d_lo + d_hi);

  // Reject a degenerate box: a single-ping mark has zero alongtrack travel, and a
  // zero-width drag has zero athwartship extent. Either yields a zero-area BOX
  // Contact that means nothing on the map -> bail (ok stays false) so the plugin
  // shows the un-markable cue instead of publishing it.
  constexpr double kMinExtentM = 1e-3;  // 1 mm
  if (alongtrack < kMinExtentM || (d_hi - d_lo) < kMinExtentM) {
    return out;  // ok = false
  }

  // Sensor-frame box corners (x = alongtrack, y = athwartship). Display range is
  // negative to port (left); REP-103 body y is +left, so sensor y = -range. The
  // sign only flips which corner is which; make_box_contact takes the AABB.
  const double half_along = 0.5 * alongtrack;
  out.corners = {
    {-half_along, -d_lo},
    {-half_along, -d_hi},
    {half_along, -d_lo},
    {half_along, -d_hi}};

  // Geodetic centroid: offset the middle ping by the centroid's sensor-frame
  // position (alongtrack 0 at the middle ping, athwartship -d_mid), rotate that
  // offset into ECEF, add the ping's ECEF origin, then ECEF->geodetic.
  const std::array<double, 3> offset = rotate(q, {0.0, -d_mid, 0.0});
  const double cx = mid_pose.translation.x + offset[0];
  const double cy = mid_pose.translation.y + offset[1];
  const double cz = mid_pose.translation.z + offset[2];

  const GeographicLib::Geocentric & earth = GeographicLib::Geocentric::WGS84();
  earth.Reverse(cx, cy, cz, out.latitude, out.longitude, out.altitude);

  out.frame = frame;
  out.stamp_s = stamps.empty() ? 0.0 : stamps[std::min(mid, stamps.size() - 1)];
  out.ok = true;
  return out;
}

}  // namespace rqt_sonar_waterfall
