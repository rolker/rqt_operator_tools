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

#ifndef RQT_MARINE_SONAR__PING_HPP_
#define RQT_MARINE_SONAR__PING_HPP_

#include <utility>

#include <marine_acoustic_msgs/msg/raw_sonar_image.hpp>

namespace rqt_marine_sonar
{

/// Thin view over a RawSonarImage that exposes the water-column geometry the
/// echogram needs: the depth span the samples cover, the per-sample bin size,
/// and a depth-indexed sample lookup. Depths are derived from the ping's sound
/// speed and sample rate (two-way travel time), so they are slant ranges read
/// as depth for a vertically-pointing single beam / nadir beam.
class Ping
{
public:
  explicit Ping(const marine_acoustic_msgs::msg::RawSonarImage & message);

  /// Depth (m) of the first sample (sample0).
  float minimumDepth() const;
  /// Depth (m) just past the last sample (sample0 + samples_per_beam).
  float maximumDepth() const;
  /// Depth (m) spanned by one sample.
  float binSize() const;

  /// Backscatter value at the given depth (m), or NaN if out of range or the
  /// image is not FLOAT32.
  float sampleAt(float depth) const;

private:
  const marine_acoustic_msgs::msg::RawSonarImage & message_;
};

/// Convenience: (minimum, maximum) depth in metres for a ping.
std::pair<float, float> depthRange(
  const marine_acoustic_msgs::msg::RawSonarImage & ping);

}  // namespace rqt_marine_sonar

#endif  // RQT_MARINE_SONAR__PING_HPP_
