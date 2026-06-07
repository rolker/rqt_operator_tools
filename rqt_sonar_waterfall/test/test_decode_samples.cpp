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
#include <cstring>
#include <type_traits>
#include <vector>

#include <marine_acoustic_msgs/msg/sonar_image_data.hpp>

#include "rqt_sonar_waterfall/waterfall_model.hpp"

namespace
{

using marine_acoustic_msgs::msg::SonarImageData;

// Serialize a typed value to bytes in the requested endianness, matching how a
// sonar would lay the sample out on the wire.
template<typename T>
void append_bytes(std::vector<uint8_t> & out, T value, bool big_endian)
{
  using UInt = std::conditional_t<sizeof(T) == 1, uint8_t,
      std::conditional_t<sizeof(T) == 2, uint16_t,
      std::conditional_t<sizeof(T) == 4, uint32_t, uint64_t>>>;
  UInt bits;
  std::memcpy(&bits, &value, sizeof(T));
  for (std::size_t i = 0; i < sizeof(UInt); ++i) {
    const std::size_t shift = big_endian ? (sizeof(UInt) - 1 - i) : i;
    out.push_back(static_cast<uint8_t>((bits >> (8 * shift)) & 0xFF));
  }
}

template<typename T>
SonarImageData make_image(uint32_t dtype, const std::vector<T> & values, bool big_endian)
{
  SonarImageData img;
  img.dtype = dtype;
  img.is_bigendian = big_endian;
  img.beam_count = 1;
  for (T v : values) {
    append_bytes(img.data, v, big_endian);
  }
  return img;
}

}  // namespace

TEST(DecodeSamples, Uint8Passthrough)
{
  auto img = make_image<uint8_t>(SonarImageData::DTYPE_UINT8, {0, 7, 255}, false);
  EXPECT_EQ(rqt_sonar_waterfall::decode_samples(img),
    (std::vector<float>{0.0f, 7.0f, 255.0f}));
}

TEST(DecodeSamples, Int8Negative)
{
  auto img = make_image<int8_t>(SonarImageData::DTYPE_INT8, {-128, -1, 0, 127}, false);
  EXPECT_EQ(rqt_sonar_waterfall::decode_samples(img),
    (std::vector<float>{-128.0f, -1.0f, 0.0f, 127.0f}));
}

TEST(DecodeSamples, Int16LittleEndian)
{
  auto img = make_image<int16_t>(SonarImageData::DTYPE_INT16,
      {-32768, -1, 0, 1, 32767}, false);
  EXPECT_EQ(rqt_sonar_waterfall::decode_samples(img),
    (std::vector<float>{-32768.0f, -1.0f, 0.0f, 1.0f, 32767.0f}));
}

TEST(DecodeSamples, Int16BigEndianMatchesLittle)
{
  const std::vector<int16_t> values{-2, -1, 0, 1, 300, 32000};
  auto le = make_image<int16_t>(SonarImageData::DTYPE_INT16, values, false);
  auto be = make_image<int16_t>(SonarImageData::DTYPE_INT16, values, true);
  EXPECT_EQ(rqt_sonar_waterfall::decode_samples(le),
    rqt_sonar_waterfall::decode_samples(be));
}

TEST(DecodeSamples, Uint16)
{
  auto img = make_image<uint16_t>(SonarImageData::DTYPE_UINT16, {0, 256, 65535}, false);
  EXPECT_EQ(rqt_sonar_waterfall::decode_samples(img),
    (std::vector<float>{0.0f, 256.0f, 65535.0f}));
}

TEST(DecodeSamples, Int32)
{
  auto img = make_image<int32_t>(SonarImageData::DTYPE_INT32, {-1, 0, 123456}, true);
  EXPECT_EQ(rqt_sonar_waterfall::decode_samples(img),
    (std::vector<float>{-1.0f, 0.0f, 123456.0f}));
}

TEST(DecodeSamples, Float32RoundTrip)
{
  auto img = make_image<float>(SonarImageData::DTYPE_FLOAT32,
      {-1.5f, 0.0f, 3.25f, 1024.5f}, false);
  EXPECT_EQ(rqt_sonar_waterfall::decode_samples(img),
    (std::vector<float>{-1.5f, 0.0f, 3.25f, 1024.5f}));
}

TEST(DecodeSamples, Float64BigEndian)
{
  auto img = make_image<double>(SonarImageData::DTYPE_FLOAT64, {2.5, -0.125}, true);
  auto out = rqt_sonar_waterfall::decode_samples(img);
  ASSERT_EQ(out.size(), 2u);
  EXPECT_FLOAT_EQ(out[0], 2.5f);
  EXPECT_FLOAT_EQ(out[1], -0.125f);
}

TEST(DecodeSamples, RaggedTrailingBytesDropped)
{
  // Five bytes for INT16 (2-byte stride) => two whole samples, last byte dropped.
  SonarImageData img;
  img.dtype = SonarImageData::DTYPE_INT16;
  img.is_bigendian = false;
  img.data = {0x01, 0x00, 0x02, 0x00, 0x03};
  auto out = rqt_sonar_waterfall::decode_samples(img);
  EXPECT_EQ(out, (std::vector<float>{1.0f, 2.0f}));
}

TEST(DecodeSamples, EmptyData)
{
  SonarImageData img;
  img.dtype = SonarImageData::DTYPE_INT16;
  EXPECT_TRUE(rqt_sonar_waterfall::decode_samples(img).empty());
}

TEST(DecodeSamples, UnknownDtypeReturnsEmpty)
{
  SonarImageData img;
  img.dtype = 999;
  img.data = {0x01, 0x02, 0x03, 0x04};
  EXPECT_TRUE(rqt_sonar_waterfall::decode_samples(img).empty());
}

TEST(DefaultFullScale, IntegerDtypesReturnTheirFullScale)
{
  using rqt_sonar_waterfall::default_full_scale;
  EXPECT_DOUBLE_EQ(default_full_scale(SonarImageData::DTYPE_UINT8), 255.0);
  EXPECT_DOUBLE_EQ(default_full_scale(SonarImageData::DTYPE_INT8), 127.0);
  EXPECT_DOUBLE_EQ(default_full_scale(SonarImageData::DTYPE_UINT16), 65535.0);
  EXPECT_DOUBLE_EQ(default_full_scale(SonarImageData::DTYPE_INT16), 32767.0);
}

TEST(DefaultFullScale, Uint16IsNotClippedByTheOldDefault)
{
  // Regression for the original bug: the manual default was 32767, which clips
  // 16-bit data (the garmin GCV-20 driver publishes UINT16 with values ~62000).
  EXPECT_GT(rqt_sonar_waterfall::default_full_scale(SonarImageData::DTYPE_UINT16),
    32767.0);
}

TEST(DefaultFullScale, WideIntegersClampToTheSpinCeiling)
{
  // 32-/64-bit full scale overflows the manual spin box (max 1e9); clamp there.
  using rqt_sonar_waterfall::default_full_scale;
  EXPECT_DOUBLE_EQ(default_full_scale(SonarImageData::DTYPE_UINT32), 1.0e9);
  EXPECT_DOUBLE_EQ(default_full_scale(SonarImageData::DTYPE_INT32), 1.0e9);
  EXPECT_DOUBLE_EQ(default_full_scale(SonarImageData::DTYPE_UINT64), 1.0e9);
  EXPECT_DOUBLE_EQ(default_full_scale(SonarImageData::DTYPE_INT64), 1.0e9);
}

TEST(DefaultFullScale, FloatsDefaultToUnity)
{
  using rqt_sonar_waterfall::default_full_scale;
  EXPECT_DOUBLE_EQ(default_full_scale(SonarImageData::DTYPE_FLOAT32), 1.0);
  EXPECT_DOUBLE_EQ(default_full_scale(SonarImageData::DTYPE_FLOAT64), 1.0);
}

TEST(DefaultFullScale, UnknownDtypeAssumesWidestCommonDepth)
{
  // An unrecognized dtype should not clip 16-bit data, the deepest we publish.
  EXPECT_DOUBLE_EQ(rqt_sonar_waterfall::default_full_scale(199), 65535.0);
}
