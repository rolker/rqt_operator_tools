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

#ifndef RQT_SONAR_WATERFALL__ROW_EXTRACTOR_HPP_
#define RQT_SONAR_WATERFALL__ROW_EXTRACTOR_HPP_

#include <optional>

#include <marine_acoustic_msgs/msg/raw_sonar_image.hpp>

#include "rqt_sonar_waterfall/waterfall_model.hpp"

namespace rqt_sonar_waterfall
{

/// Strategy that turns one RawSonarImage ping into one WaterfallRow.
///
/// Different sonar geometries map to the waterfall differently (sidescan keeps
/// the sample series of a single beam; multibeam backscatter collapses each
/// beam's samples to one value). Each geometry is a RowExtractor implementation
/// so the waterfall widget stays geometry-agnostic.
class RowExtractor
{
public:
  virtual ~RowExtractor() = default;

  /// Whether this extractor can handle the given ping's geometry.
  virtual bool accepts(
    const marine_acoustic_msgs::msg::RawSonarImage & msg) const = 0;

  /// Extract a row, or std::nullopt if the ping is not acceptable.
  virtual std::optional<WaterfallRow> extract(
    const marine_acoustic_msgs::msg::RawSonarImage & msg) const = 0;
};

/// Sidescan extractor: one beam, samples taken as the across-track profile.
///
/// Accepts pings with `beam_count` 0 or 1 (a single beam). The row is the full
/// decoded sample series; `range_max` is the slant range of the last sample,
/// `sound_speed * samples / (2 * sample_rate)`, or 0 when those fields are
/// unavailable.
class SingleBeamExtractor : public RowExtractor
{
public:
  bool accepts(
    const marine_acoustic_msgs::msg::RawSonarImage & msg) const override;

  std::optional<WaterfallRow> extract(
    const marine_acoustic_msgs::msg::RawSonarImage & msg) const override;
};

}  // namespace rqt_sonar_waterfall

#endif  // RQT_SONAR_WATERFALL__ROW_EXTRACTOR_HPP_
