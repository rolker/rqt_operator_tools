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

#include "rqt_sonar_waterfall/row_extractor.hpp"

#include <algorithm>
#include <cstddef>

#include "rqt_sonar_waterfall/waterfall_model.hpp"

namespace rqt_sonar_waterfall
{

bool SingleBeamExtractor::accepts(
  const marine_acoustic_msgs::msg::RawSonarImage & msg) const
{
  // beam_count 0 (unset by some drivers) or 1 is a single across-track beam.
  return msg.image.beam_count <= 1;
}

std::optional<WaterfallRow> SingleBeamExtractor::extract(
  const marine_acoustic_msgs::msg::RawSonarImage & msg) const
{
  if (!accepts(msg)) {
    return std::nullopt;
  }

  WaterfallRow row;
  row.intensities = decode_samples(msg.image);

  // Number of range bins for the slant-range axis. Prefer the declared count,
  // but never exceed what actually decoded: on a truncated/short ping the
  // declared count would overstate the displayed range.
  std::size_t bins =
    msg.samples_per_beam > 0 ? msg.samples_per_beam : row.intensities.size();
  if (!row.intensities.empty()) {
    bins = std::min(bins, row.intensities.size());
  }
  if (msg.sample_rate > 0.0f && msg.ping_info.sound_speed > 0.0f && bins > 0) {
    row.range_max = static_cast<double>(msg.ping_info.sound_speed) *
      static_cast<double>(bins) /
      (2.0 * static_cast<double>(msg.sample_rate));
  }

  row.stamp = static_cast<double>(msg.header.stamp.sec) +
    static_cast<double>(msg.header.stamp.nanosec) * 1e-9;
  // Keep the unflattened header stamp for an exact TF lookup (issue #86).
  row.stamp_time = msg.header.stamp;
  // Carry the sensor frame so the plugin can resolve the earth<-sensor pose for
  // target marking (issue #86). post_row() does the TF lookup against this frame.
  row.sensor_frame = msg.header.frame_id;
  return row;
}

}  // namespace rqt_sonar_waterfall
