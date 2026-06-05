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

#include <cstdint>
#include <vector>

#include <marine_acoustic_msgs/msg/raw_sonar_image.hpp>
#include <marine_acoustic_msgs/msg/sonar_image_data.hpp>

#include "rqt_sonar_waterfall/row_extractor.hpp"

namespace
{

using marine_acoustic_msgs::msg::RawSonarImage;
using marine_acoustic_msgs::msg::SonarImageData;

// A single-beam ping of `n` INT16 samples (little-endian), plus optional
// sound-speed / sample-rate so the range axis can be exercised.
RawSonarImage make_ping(
  uint32_t beam_count, std::size_t n,
  float sample_rate = 0.0f, float sound_speed = 0.0f)
{
  RawSonarImage msg;
  msg.image.dtype = SonarImageData::DTYPE_INT16;
  msg.image.is_bigendian = false;
  msg.image.beam_count = beam_count;
  msg.samples_per_beam = static_cast<uint32_t>(n);
  msg.sample_rate = sample_rate;
  msg.ping_info.sound_speed = sound_speed;
  for (std::size_t i = 0; i < n; ++i) {
    const auto v = static_cast<int16_t>(i);
    msg.image.data.push_back(static_cast<uint8_t>(v & 0xFF));
    msg.image.data.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
  }
  return msg;
}

}  // namespace

TEST(SingleBeamExtractor, AcceptsZeroOrOneBeam)
{
  rqt_sonar_waterfall::SingleBeamExtractor ex;
  EXPECT_TRUE(ex.accepts(make_ping(0, 4)));
  EXPECT_TRUE(ex.accepts(make_ping(1, 4)));
  EXPECT_FALSE(ex.accepts(make_ping(2, 4)));
  EXPECT_FALSE(ex.accepts(make_ping(256, 4)));
}

TEST(SingleBeamExtractor, ExtractsSampleSeries)
{
  rqt_sonar_waterfall::SingleBeamExtractor ex;
  auto row = ex.extract(make_ping(1, 5));
  ASSERT_TRUE(row.has_value());
  EXPECT_EQ(row->intensities,
    (std::vector<float>{0.0f, 1.0f, 2.0f, 3.0f, 4.0f}));
}

TEST(SingleBeamExtractor, MultibeamRejected)
{
  rqt_sonar_waterfall::SingleBeamExtractor ex;
  EXPECT_FALSE(ex.extract(make_ping(64, 8)).has_value());
}

TEST(SingleBeamExtractor, RangeFromSoundSpeedAndRate)
{
  rqt_sonar_waterfall::SingleBeamExtractor ex;
  // range = sound_speed * samples / (2 * sample_rate)
  //       = 1500 * 1000 / (2 * 7500) = 100 m
  auto row = ex.extract(make_ping(1, 1000, /*sample_rate=*/7500.0f,
    /*sound_speed=*/1500.0f));
  ASSERT_TRUE(row.has_value());
  EXPECT_DOUBLE_EQ(row->range_max, 100.0);
}

TEST(SingleBeamExtractor, RangeUnknownWhenRateZero)
{
  rqt_sonar_waterfall::SingleBeamExtractor ex;
  auto row = ex.extract(make_ping(1, 100, /*sample_rate=*/0.0f,
    /*sound_speed=*/1500.0f));
  ASSERT_TRUE(row.has_value());
  EXPECT_DOUBLE_EQ(row->range_max, 0.0);
}

TEST(SingleBeamExtractor, StampFromHeader)
{
  rqt_sonar_waterfall::SingleBeamExtractor ex;
  auto msg = make_ping(1, 4);
  msg.header.stamp.sec = 42;
  msg.header.stamp.nanosec = 500000000u;
  auto row = ex.extract(msg);
  ASSERT_TRUE(row.has_value());
  EXPECT_DOUBLE_EQ(row->stamp, 42.5);
}
