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

#include "rqt_sonar_waterfall/waterfall_model.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

namespace rqt_sonar_waterfall
{

namespace
{

// Read one sample of width sizeof(Bits) at `p`, assembling the bytes in the
// requested endianness into `Bits`, reinterpreting as `Value`, and returning it
// as a float. `Bits` is the unsigned integer of the same width as `Value`; the
// memcpy gives correct two's-complement (signed ints) and IEEE-754 (floats)
// reinterpretation without relying on host endianness.
template<typename Bits, typename Value>
float read_sample(const uint8_t * p, bool big_endian)
{
  static_assert(sizeof(Bits) == sizeof(Value), "width mismatch");
  Bits bits = 0;
  for (std::size_t i = 0; i < sizeof(Bits); ++i) {
    const std::size_t shift = big_endian ? (sizeof(Bits) - 1 - i) : i;
    bits |= static_cast<Bits>(p[i]) << (8 * shift);
  }
  Value value;
  std::memcpy(&value, &bits, sizeof(Value));
  return static_cast<float>(value);
}

template<typename Bits, typename Value>
std::vector<float> decode_typed(const std::vector<uint8_t> & data, bool big_endian)
{
  const std::size_t stride = sizeof(Bits);
  const std::size_t count = data.size() / stride;  // trailing partial sample dropped
  std::vector<float> out;
  out.reserve(count);
  for (std::size_t i = 0; i < count; ++i) {
    out.push_back(read_sample<Bits, Value>(data.data() + i * stride, big_endian));
  }
  return out;
}

}  // namespace

std::vector<float> decode_samples(
  const marine_acoustic_msgs::msg::SonarImageData & image)
{
  using Img = marine_acoustic_msgs::msg::SonarImageData;
  const bool be = image.is_bigendian;
  const auto & data = image.data;

  switch (image.dtype) {
    case Img::DTYPE_UINT8:
      return decode_typed<uint8_t, uint8_t>(data, be);
    case Img::DTYPE_INT8:
      return decode_typed<uint8_t, int8_t>(data, be);
    case Img::DTYPE_UINT16:
      return decode_typed<uint16_t, uint16_t>(data, be);
    case Img::DTYPE_INT16:
      return decode_typed<uint16_t, int16_t>(data, be);
    case Img::DTYPE_UINT32:
      return decode_typed<uint32_t, uint32_t>(data, be);
    case Img::DTYPE_INT32:
      return decode_typed<uint32_t, int32_t>(data, be);
    case Img::DTYPE_UINT64:
      return decode_typed<uint64_t, uint64_t>(data, be);
    case Img::DTYPE_INT64:
      return decode_typed<uint64_t, int64_t>(data, be);
    case Img::DTYPE_FLOAT32:
      return decode_typed<uint32_t, float>(data, be);
    case Img::DTYPE_FLOAT64:
      return decode_typed<uint64_t, double>(data, be);
    default:
      return {};
  }
}

double default_full_scale(uint32_t dtype)
{
  using Img = marine_acoustic_msgs::msg::SonarImageData;
  switch (dtype) {
    case Img::DTYPE_UINT8:
      return 255.0;
    case Img::DTYPE_INT8:
      return 127.0;
    case Img::DTYPE_UINT16:
      return 65535.0;
    case Img::DTYPE_INT16:
      return 32767.0;
    case Img::DTYPE_UINT32:
    case Img::DTYPE_INT32:
    case Img::DTYPE_UINT64:
    case Img::DTYPE_INT64:
      // True full scale (>= 2^31) overflows the manual-range spin box (1e9);
      // clamp to its ceiling. Auto-range is the practical choice this wide.
      return 1.0e9;
    case Img::DTYPE_FLOAT32:
    case Img::DTYPE_FLOAT64:
      // Floats have no fixed full scale; 1.0 fits the common normalized case
      // and is just a starting point the operator can override.
      return 1.0;
    default:
      // Unknown dtype: assume the widest common integer depth so 16-bit data
      // (the deepest the driver fleet produces) is not clipped.
      return 65535.0;
  }
}

std::optional<WaterfallRow> combine_rows(
  const std::optional<WaterfallRow> & port,
  const std::optional<WaterfallRow> & starboard)
{
  if (!port && !starboard) {
    return std::nullopt;
  }
  if (port && !starboard) {
    return port;
  }
  if (!port && starboard) {
    return starboard;
  }

  WaterfallRow combined;
  combined.intensities.reserve(
    port->intensities.size() + starboard->intensities.size());
  // Port reversed: nadir (sample 0) ends up adjacent to the center boundary.
  combined.intensities.assign(
    port->intensities.rbegin(), port->intensities.rend());
  combined.intensities.insert(
    combined.intensities.end(),
    starboard->intensities.begin(), starboard->intensities.end());
  combined.range_max = std::max(port->range_max, starboard->range_max);
  combined.stamp = std::max(port->stamp, starboard->stamp);
  return combined;
}

}  // namespace rqt_sonar_waterfall
