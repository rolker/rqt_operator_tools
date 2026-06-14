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
#include <cmath>
#include <cstddef>
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

  WaterfallRow combined;
  const std::size_t port_n = port ? port->intensities.size() : 0;
  const std::size_t stbd_n = starboard ? starboard->intensities.size() : 0;
  combined.intensities.reserve(port_n + stbd_n);
  // Port reversed and placed left of nadir; starboard in natural order right of
  // it. A silent side contributes nothing, so nadir_index (= port count) still
  // places nadir correctly: port-only -> all left, starboard-only -> all right.
  if (port) {
    combined.intensities.assign(
      port->intensities.rbegin(), port->intensities.rend());
  }
  if (starboard) {
    combined.intensities.insert(
      combined.intensities.end(),
      starboard->intensities.begin(), starboard->intensities.end());
  }
  combined.nadir_index = port_n;
  combined.range_max_port = port ? port->range_max : 0.0;
  combined.range_max_stbd = starboard ? starboard->range_max : 0.0;
  combined.range_max = std::max(combined.range_max_port, combined.range_max_stbd);
  // Altitude: prefer whichever side carries one (they share the nadir depth).
  combined.altitude = (port && port->altitude > 0.0) ? port->altitude :
    (starboard ? starboard->altitude : 0.0);
  combined.stamp = std::max(
    port ? port->stamp : 0.0, starboard ? starboard->stamp : 0.0);
  return combined;
}

double ground_range(double slant_range, double altitude)
{
  if (altitude <= 0.0) {
    return slant_range;
  }
  const double g2 = slant_range * slant_range - altitude * altitude;
  return g2 > 0.0 ? std::sqrt(g2) : 0.0;
}

namespace
{

// Nearest sample on a side's slant axis, or a negative sentinel when the column
// has no data (range unknown, or slant beyond the side's reach). `count` is the
// number of samples on the side; `base` is the storage index of the side's
// nadir sample; `step` is +1 (starboard, nadir->far) or -1 (reversed port,
// nadir at base, far toward index 0).
std::ptrdiff_t sample_on_side(
  double slant, double side_range, std::size_t count, std::ptrdiff_t base,
  std::ptrdiff_t step)
{
  if (side_range <= 0.0 || count == 0 || slant > side_range) {
    return -1;
  }
  const double frac = slant / side_range;  // 0 at nadir, 1 at far
  const std::ptrdiff_t offset = (count > 1) ?
    static_cast<std::ptrdiff_t>(std::lround(frac * static_cast<double>(count - 1))) :
    0;
  return base + step * offset;
}

}  // namespace

std::vector<float> project_row(
  const std::vector<float> & samples, std::size_t nadir_index,
  double range_port, double range_stbd, double altitude, bool ground,
  double half_width, std::size_t columns)
{
  std::vector<float> out(columns, 0.0f);
  if (columns == 0 || samples.empty() || half_width <= 0.0) {
    return out;
  }
  const std::size_t port_n = std::min(nadir_index, samples.size());
  const std::size_t stbd_n = samples.size() - port_n;
  const double center = static_cast<double>(columns - 1) / 2.0;

  for (std::size_t x = 0; x < columns; ++x) {
    // Signed display range for this column: -half_width (left) .. +half_width.
    const double d = (center > 0.0) ?
      (static_cast<double>(x) - center) / center * half_width :
      0.0;
    const double mag = std::abs(d);
    const double slant = ground ? std::sqrt(mag * mag + altitude * altitude) : mag;

    // At nadir (d == 0) the sample is the shared centre point; assign it to
    // whichever side has data, preferring port, so a single-sided ping still
    // fills the centre column instead of going black.
    const bool use_port = (d < 0.0) || (d == 0.0 && range_port > 0.0);
    std::ptrdiff_t idx = -1;
    if (use_port) {
      // Port: stored reversed in [0, port_n); nadir at port_n-1, far at 0.
      idx = sample_on_side(
        slant, range_port, port_n, static_cast<std::ptrdiff_t>(port_n) - 1, -1);
    } else {
      // Starboard: [port_n, size()); nadir at port_n, far at end.
      idx = sample_on_side(
        slant, range_stbd, stbd_n, static_cast<std::ptrdiff_t>(port_n), +1);
    }
    if (idx >= 0 && static_cast<std::size_t>(idx) < samples.size()) {
      out[x] = samples[static_cast<std::size_t>(idx)];
    }
  }
  return out;
}

std::vector<float> apply_tvg(
  const std::vector<float> & samples, std::size_t nadir_index,
  double range_port, double range_stbd, double slope, double ref_range)
{
  std::vector<float> out = samples;
  if (slope <= 0.0 || ref_range <= 0.0) {
    return out;  // identity
  }
  const std::size_t port_n = std::min(nadir_index, samples.size());
  const std::size_t stbd_n = samples.size() - port_n;

  // Per-sample slant range: bin centers at (k + 0.5)/count of the side range.
  // Port is reversed, so storage index s in [0, port_n) is original bin
  // (port_n-1 - s); starboard storage index (port_n + k) is original bin k.
  for (std::size_t s = 0; s < port_n; ++s) {
    if (range_port <= 0.0 || port_n == 0) {
      break;
    }
    const std::size_t bin = port_n - 1 - s;
    const double slant = range_port * (static_cast<double>(bin) + 0.5) /
      static_cast<double>(port_n);
    const double factor = std::pow(std::max(slant, ref_range) / ref_range, slope);
    out[s] = static_cast<float>(static_cast<double>(out[s]) * factor);
  }
  for (std::size_t k = 0; k < stbd_n; ++k) {
    if (range_stbd <= 0.0) {
      break;
    }
    const double slant = range_stbd * (static_cast<double>(k) + 0.5) /
      static_cast<double>(stbd_n);
    const double factor = std::pow(std::max(slant, ref_range) / ref_range, slope);
    out[port_n + k] = static_cast<float>(static_cast<double>(out[port_n + k]) * factor);
  }
  return out;
}

}  // namespace rqt_sonar_waterfall
