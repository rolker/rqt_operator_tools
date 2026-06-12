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
#include <cstdint>
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

TEST(PingTest, SamplesDecodeFloat)
{
  const auto msg = makeFloatPing({10.0f, 20.0f, 30.0f, 40.0f}, /*sample0=*/0);
  rqt_marine_sonar::Ping ping(msg);

  const auto samples = ping.samples();
  ASSERT_EQ(samples.size(), 4u);
  EXPECT_FLOAT_EQ(samples[0], 10.0f);
  EXPECT_FLOAT_EQ(samples[3], 40.0f);
}

TEST(PingTest, TruncatedDataDecodesOnlyCompleteSamples)
{
  // samples_per_beam claims 4 samples but only 2 floats of data arrived; the
  // decode is bounded by the bytes that actually arrived, so consumers can
  // never over-read past the buffer.
  auto msg = makeFloatPing({1.0f, 2.0f}, /*sample0=*/0);
  msg.samples_per_beam = 4;  // lie about the count; data still holds 2 floats

  rqt_marine_sonar::Ping ping(msg);
  EXPECT_EQ(ping.samples().size(), 2u);
}

TEST(PingTest, IntegerDtypesDecode)
{
  // Regression for rqt_operator_tools#54: the GCV sidescan publishes UINT16
  // (GCV-10: UINT8) raw counts, which the pre-modernization plugin silently
  // discarded -- a blank echogram with a healthy stream.
  marine_acoustic_msgs::msg::RawSonarImage msg;
  msg.ping_info.sound_speed = 1500.0f;
  msg.sample_rate = 1000.0f;
  msg.sample0 = 0;
  msg.samples_per_beam = 3;
  msg.image.dtype = marine_acoustic_msgs::msg::SonarImageData::DTYPE_UINT16;
  msg.image.is_bigendian = false;
  msg.image.data = {0x7d, 0x00, 0xdd, 0x2a, 0xfc, 0x92};  // 125, 10973, 37628 LE

  rqt_marine_sonar::Ping ping(msg);
  auto samples = ping.samples();
  ASSERT_EQ(samples.size(), 3u);
  EXPECT_FLOAT_EQ(samples[0], 125.0f);
  EXPECT_FLOAT_EQ(samples[1], 10973.0f);
  EXPECT_FLOAT_EQ(samples[2], 37628.0f);

  msg.image.dtype = marine_acoustic_msgs::msg::SonarImageData::DTYPE_UINT8;
  msg.image.data = {0, 128, 255};
  rqt_marine_sonar::Ping ping8(msg);
  samples = ping8.samples();
  ASSERT_EQ(samples.size(), 3u);
  EXPECT_FLOAT_EQ(samples[0], 0.0f);
  EXPECT_FLOAT_EQ(samples[1], 128.0f);
  EXPECT_FLOAT_EQ(samples[2], 255.0f);

  msg.image.dtype = marine_acoustic_msgs::msg::SonarImageData::DTYPE_INT16;
  msg.image.data = {0xff, 0xff, 0x00, 0x80};  // -1, -32768 LE
  rqt_marine_sonar::Ping ping16(msg);
  samples = ping16.samples();
  ASSERT_EQ(samples.size(), 2u);
  EXPECT_FLOAT_EQ(samples[0], -1.0f);
  EXPECT_FLOAT_EQ(samples[1], -32768.0f);
}

TEST(PingTest, BigEndianDecode)
{
  // A single FLOAT32 sample encoded big-endian; with is_bigendian set, the
  // decode must reassemble the bytes correctly (host-byte-order independent).
  marine_acoustic_msgs::msg::RawSonarImage msg;
  msg.ping_info.sound_speed = 1500.0f;
  msg.sample_rate = 1000.0f;
  msg.sample0 = 0;
  msg.samples_per_beam = 1;
  msg.image.dtype = marine_acoustic_msgs::msg::SonarImageData::DTYPE_FLOAT32;
  msg.image.is_bigendian = true;

  const float expected = 42.5f;
  uint32_t bits;
  std::memcpy(&bits, &expected, sizeof(bits));
  msg.image.data = {
    static_cast<uint8_t>((bits >> 24) & 0xFF),
    static_cast<uint8_t>((bits >> 16) & 0xFF),
    static_cast<uint8_t>((bits >> 8) & 0xFF),
    static_cast<uint8_t>(bits & 0xFF),
  };

  rqt_marine_sonar::Ping ping(msg);
  const auto samples = ping.samples();
  ASSERT_EQ(samples.size(), 1u);
  EXPECT_FLOAT_EQ(samples[0], expected);
}
