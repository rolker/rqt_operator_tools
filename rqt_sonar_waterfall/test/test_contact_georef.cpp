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

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include <GeographicLib/Geocentric.hpp>
#include <geometry_msgs/msg/transform.hpp>

#include "rqt_sonar_waterfall/contact_georef.hpp"

namespace
{

using rqt_sonar_waterfall::georeference_box;

geometry_msgs::msg::Transform pose_at(
  double x, double y, double z,
  double qx = 0.0, double qy = 0.0, double qz = 0.0, double qw = 1.0)
{
  geometry_msgs::msg::Transform t;
  t.translation.x = x;
  t.translation.y = y;
  t.translation.z = z;
  t.rotation.x = qx;
  t.rotation.y = qy;
  t.rotation.z = qz;
  t.rotation.w = qw;
  return t;
}

// AABB extent of the returned corners (x = alongtrack, y = athwartship).
void corner_extent(
  const rqt_sonar_waterfall::GeorefBox & g, double & dx, double & dy)
{
  double min_x = g.corners.front().x, max_x = min_x;
  double min_y = g.corners.front().y, max_y = min_y;
  for (const auto & p : g.corners) {
    min_x = std::min(min_x, p.x);
    max_x = std::max(max_x, p.x);
    min_y = std::min(min_y, p.y);
    max_y = std::max(max_y, p.y);
  }
  dx = max_x - min_x;
  dy = max_y - min_y;
}

}  // namespace

TEST(ContactGeoref, EmptyPosesNotOk)
{
  auto g = georeference_box({}, {}, "frame", -5.0, 5.0);
  EXPECT_FALSE(g.ok);
}

TEST(ContactGeoref, DegenerateQuaternionNotOk)
{
  // An all-zero quaternion (no usable orientation) must not produce a contact.
  auto g = georeference_box({pose_at(0, 0, 0, 0, 0, 0, 0)}, {1.0}, "frame", -5.0, 5.0);
  EXPECT_FALSE(g.ok);
}

TEST(ContactGeoref, SinglePingMarkIsDegenerateNotOk)
{
  // A single ping has zero alongtrack travel -> zero-area box. Reject it so the
  // plugin shows the un-markable cue instead of publishing a meaningless contact.
  auto g = georeference_box({pose_at(100.0, 0.0, 0.0)}, {5.0}, "frame", -10.0, 20.0);
  EXPECT_FALSE(g.ok);
}

TEST(ContactGeoref, ZeroWidthDragIsDegenerateNotOk)
{
  // Two pings (real alongtrack) but zero athwartship width -> still zero-area.
  std::vector<geometry_msgs::msg::Transform> poses = {
    pose_at(100.0, 0.0, 0.0),
    pose_at(108.0, 0.0, 0.0)};
  auto g = georeference_box(poses, {10.0, 12.0}, "frame", 5.0, 5.0);
  EXPECT_FALSE(g.ok);
}

TEST(ContactGeoref, BoxExtentIsAthwartByAlongtrack)
{
  // Two pings 8 m apart in ECEF -> alongtrack length 8 m; range span 30 m wide.
  std::vector<geometry_msgs::msg::Transform> poses = {
    pose_at(100.0, 0.0, 0.0),
    pose_at(108.0, 0.0, 0.0)};
  std::vector<double> stamps = {10.0, 12.0};
  auto g = georeference_box(poses, stamps, "bizzy/sidescan", -10.0, 20.0);
  ASSERT_TRUE(g.ok);
  ASSERT_EQ(g.corners.size(), 4u);

  double along = 0.0, athwart = 0.0;
  corner_extent(g, along, athwart);
  EXPECT_NEAR(along, 8.0, 1e-9);    // alongtrack = ping travel
  EXPECT_NEAR(athwart, 30.0, 1e-9);  // athwartship = |20 - (-10)|
  EXPECT_EQ(g.frame, "bizzy/sidescan");
  EXPECT_DOUBLE_EQ(g.stamp_s, 12.0);  // middle ping (index 1 of 2)
}

TEST(ContactGeoref, CentroidIdentityRotationAddsAthwartOffset)
{
  // Identity rotation -> sensor axes == ECEF axes. The centroid sits at the
  // middle ping's origin offset by -d_mid along ECEF Y (sensor +Y = port).
  const GeographicLib::Geocentric & earth = GeographicLib::Geocentric::WGS84();
  double ox, oy, oz;
  earth.Forward(43.0, -71.0, 50.0, ox, oy, oz);

  // Symmetric box across track -> d_mid = 0 -> centroid at the ping origin. Use
  // two pings (the centroid anchors on the middle = poses[1], here the origin) so
  // the mark has a real alongtrack extent and is not rejected as degenerate;
  // poses[0] only sets the alongtrack length and does not move the centroid.
  auto g0 = georeference_box(
    {pose_at(ox - 4.0, oy, oz), pose_at(ox, oy, oz)}, {4.0, 5.0}, "f", -15.0, 15.0);
  ASSERT_TRUE(g0.ok);
  double lat0, lon0, h0;
  earth.Reverse(ox, oy, oz, lat0, lon0, h0);
  EXPECT_NEAR(g0.latitude, lat0, 1e-7);
  EXPECT_NEAR(g0.longitude, lon0, 1e-7);

  // Offset box (10..40 m starboard side) -> d_mid = 25 -> centroid at -25 in Y.
  auto g1 = georeference_box(
    {pose_at(ox - 4.0, oy, oz), pose_at(ox, oy, oz)}, {4.0, 5.0}, "f", 10.0, 40.0);
  ASSERT_TRUE(g1.ok);
  double elat, elon, eh;
  earth.Reverse(ox, oy - 25.0, oz, elat, elon, eh);
  EXPECT_NEAR(g1.latitude, elat, 1e-7);
  EXPECT_NEAR(g1.longitude, elon, 1e-7);
}

TEST(ContactGeoref, CentroidRotationRotatesOffset)
{
  // 90 deg about ECEF Z maps the sensor's -Y offset (0,-d_mid,0) to (+d_mid,0,0):
  // rotating a vector by +90 deg about Z sends (x,y) -> (-y,x), so (0,-25)->(25,0).
  const GeographicLib::Geocentric & earth = GeographicLib::Geocentric::WGS84();
  double ox, oy, oz;
  earth.Forward(43.0, -71.0, 50.0, ox, oy, oz);

  const double s = std::sin(M_PI / 4.0);  // quaternion for 90 deg about +Z
  // Two pings; the centroid + rotation anchor on the middle pose (poses[1], the
  // 90 deg one). poses[0] (identity) only gives the mark a real alongtrack extent.
  auto g = georeference_box(
    {pose_at(ox - 4.0, oy, oz), pose_at(ox, oy, oz, 0.0, 0.0, s, s)},
    {4.0, 5.0}, "f", 10.0, 40.0);  // d_mid = 25
  ASSERT_TRUE(g.ok);

  double elat, elon, eh;
  earth.Reverse(ox + 25.0, oy, oz, elat, elon, eh);
  EXPECT_NEAR(g.latitude, elat, 1e-7);
  EXPECT_NEAR(g.longitude, elon, 1e-7);
}
