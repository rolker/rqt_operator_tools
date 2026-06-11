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
#include <cstring>
#include <vector>

#include <marine_acoustic_msgs/msg/raw_sonar_image.hpp>

#include "rqt_marine_sonar/ping.hpp"

namespace
{

// A FLOAT32 single-beam ping: 1500 m/s sound speed, 1000 Hz sample rate, so each
// sample spans 0.5 * 1500 / 1000 = 0.75 m of depth (two-way travel time).
marine_acoustic_msgs::msg::RawSonarImage makeFloatPing(
  const std::vector<float> & samples, uint32_t sample0)
{
  marine_acoustic_msgs::msg::RawSonarImage msg;
  msg.ping_info.sound_speed = 1500.0f;
  msg.sample_rate = 1000.0f;
  msg.sample0 = sample0;
  msg.samples_per_beam = static_cast<uint32_t>(samples.size());
  msg.image.dtype = marine_acoustic_msgs::msg::SonarImageData::DTYPE_FLOAT32;
  msg.image.data.resize(samples.size() * sizeof(float));
  std::memcpy(msg.image.data.data(), samples.data(), samples.size() * sizeof(float));
  return msg;
}

}  // namespace

TEST(PingTest, DepthGeometry)
{
  const auto msg = makeFloatPing({1.0f, 2.0f, 3.0f, 4.0f}, /*sample0=*/0);
  rqt_marine_sonar::Ping ping(msg);

  EXPECT_FLOAT_EQ(ping.binSize(), 0.75f);
  EXPECT_FLOAT_EQ(ping.minimumDepth(), 0.0f);
  EXPECT_FLOAT_EQ(ping.maximumDepth(), 3.0f);  // 0.5 * 1500 * 4 / 1000
}

TEST(PingTest, DepthGeometryWithSampleOffset)
{
  const auto msg = makeFloatPing({1.0f, 2.0f}, /*sample0=*/10);
  rqt_marine_sonar::Ping ping(msg);

  // minimum = 0.5 * 1500 * 10 / 1000 = 7.5; span of 2 samples = 1.5
  EXPECT_FLOAT_EQ(ping.minimumDepth(), 7.5f);
  EXPECT_FLOAT_EQ(ping.maximumDepth(), 9.0f);

  const auto range = rqt_marine_sonar::depthRange(msg);
  EXPECT_FLOAT_EQ(range.first, 7.5f);
  EXPECT_FLOAT_EQ(range.second, 9.0f);
}

TEST(PingTest, SampleLookupReturnsBinnedValue)
{
  const auto msg = makeFloatPing({10.0f, 20.0f, 30.0f, 40.0f}, /*sample0=*/0);
  rqt_marine_sonar::Ping ping(msg);

  // bin size 0.75 m: depth 0 -> index 0, depth 0.8 -> index 1, depth 2.2 -> index 2
  EXPECT_FLOAT_EQ(ping.sampleAt(0.0f), 10.0f);
  EXPECT_FLOAT_EQ(ping.sampleAt(0.8f), 20.0f);
  EXPECT_FLOAT_EQ(ping.sampleAt(2.2f), 30.0f);
}

TEST(PingTest, SampleOutOfRangeIsNan)
{
  const auto msg = makeFloatPing({1.0f, 2.0f}, /*sample0=*/0);
  rqt_marine_sonar::Ping ping(msg);

  EXPECT_TRUE(std::isnan(ping.sampleAt(-1.0f)));
  EXPECT_TRUE(std::isnan(ping.sampleAt(100.0f)));
}

TEST(PingTest, SampleAtUpperBoundIsNan)
{
  // At exactly maximumDepth() the index would be samples_per_beam (one past the
  // last sample); the half-open interval must return NaN, not over-read.
  const auto msg = makeFloatPing({1.0f, 2.0f, 3.0f, 4.0f}, /*sample0=*/0);
  rqt_marine_sonar::Ping ping(msg);

  EXPECT_TRUE(std::isnan(ping.sampleAt(ping.maximumDepth())));
}

TEST(PingTest, TruncatedDataIsNan)
{
  // samples_per_beam claims 4 samples but only 2 floats of data arrived; reads
  // beyond the actual buffer must return NaN rather than over-reading.
  auto msg = makeFloatPing({1.0f, 2.0f}, /*sample0=*/0);
  msg.samples_per_beam = 4;  // lie about the count; data still holds 2 floats

  rqt_marine_sonar::Ping ping(msg);
  // Index 0 and 1 are backed by real data; 2 and 3 are past the buffer.
  EXPECT_FLOAT_EQ(ping.sampleAt(0.0f), 1.0f);
  EXPECT_TRUE(std::isnan(ping.sampleAt(2.0f)));   // index 2 -> beyond data
}

TEST(PingTest, NonFloatDtypeIsNan)
{
  auto msg = makeFloatPing({1.0f, 2.0f}, /*sample0=*/0);
  msg.image.dtype = marine_acoustic_msgs::msg::SonarImageData::DTYPE_UINT8;
  rqt_marine_sonar::Ping ping(msg);

  EXPECT_TRUE(std::isnan(ping.sampleAt(0.0f)));
}
