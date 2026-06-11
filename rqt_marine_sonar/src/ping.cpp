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

#include "rqt_marine_sonar/ping.hpp"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <utility>

namespace rqt_marine_sonar
{

Ping::Ping(const marine_acoustic_msgs::msg::RawSonarImage & message)
: message_(message)
{
}

float Ping::minimumDepth() const
{
  return 0.5 * message_.ping_info.sound_speed * message_.sample0 / message_.sample_rate;
}

float Ping::maximumDepth() const
{
  return 0.5 * message_.ping_info.sound_speed *
         (message_.sample0 + message_.samples_per_beam) / message_.sample_rate;
}

float Ping::binSize() const
{
  return 0.5 * message_.ping_info.sound_speed / message_.sample_rate;
}

float Ping::sampleAt(float depth) const
{
  if (message_.image.dtype == marine_acoustic_msgs::msg::SonarImageData::DTYPE_FLOAT32) {
    // Half-open [minimumDepth, maximumDepth): at exactly maximumDepth the index
    // would equal samples_per_beam, one past the last sample.
    if (depth >= minimumDepth() && depth < maximumDepth()) {
      const int index = static_cast<int>((depth - minimumDepth()) / binSize());
      // samples_per_beam is a wire field; bound the read against the bytes that
      // actually arrived (a truncated/malformed message must not over-read).
      const size_t float_count = message_.image.data.size() / sizeof(float);
      if (index >= 0 && static_cast<size_t>(index) < float_count) {
        // Assemble the 4 bytes per is_bigendian and memcpy into the float,
        // rather than reinterpret_cast'ing the uint8 blob (which would be a
        // strict-aliasing violation and could fault on strict-alignment archs).
        const uint8_t * p = message_.image.data.data() + static_cast<size_t>(index) * sizeof(float);
        uint32_t bits;
        if (message_.image.is_bigendian) {
          bits = (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
            (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
        } else {
          bits = (static_cast<uint32_t>(p[3]) << 24) | (static_cast<uint32_t>(p[2]) << 16) |
            (static_cast<uint32_t>(p[1]) << 8) | static_cast<uint32_t>(p[0]);
        }
        float value;
        std::memcpy(&value, &bits, sizeof(value));
        return value;
      }
    }
  }
  return std::nan("");
}

std::pair<float, float> depthRange(
  const marine_acoustic_msgs::msg::RawSonarImage & ping)
{
  float min_depth = 0.5 * ping.ping_info.sound_speed * ping.sample0 / ping.sample_rate;
  float max_depth = 0.5 * ping.ping_info.sound_speed *
    (ping.sample0 + ping.samples_per_beam) / ping.sample_rate;
  return std::make_pair(min_depth, max_depth);
}

}  // namespace rqt_marine_sonar
