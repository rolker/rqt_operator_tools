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

#ifndef RQT_SONAR_WATERFALL__WATERFALL_MODEL_HPP_
#define RQT_SONAR_WATERFALL__WATERFALL_MODEL_HPP_

#include <optional>
#include <vector>

#include <marine_acoustic_msgs/msg/sonar_image_data.hpp>

namespace rqt_sonar_waterfall
{

/// One assembled waterfall row: the across-track intensity profile for a single
/// instant, plus the metadata needed to scale and order it.
///
/// Geometry-agnostic. For sidescan a row is one beam's sample series (optionally
/// the port|starboard combination); for multibeam (deferred, #40) it is one
/// value per beam. Intensities are the raw decoded sample values — gain,
/// contrast and color mapping are applied at render time.
struct WaterfallRow
{
  /// Across-track samples. For a combined sidescan row, index order runs
  /// port-far -> nadir -> starboard-far (see combine_rows()).
  std::vector<float> intensities;

  /// Maximum slant range represented, in meters. 0 if unknown.
  double range_max = 0.0;

  /// Acquisition time in seconds since the epoch. 0 if unknown.
  double stamp = 0.0;
};

/// Decode a SonarImageData blob into per-sample float values.
///
/// Honors `dtype` (UINT8/INT8/UINT16/INT16/UINT32/INT32/UINT64/INT64/FLOAT32/
/// FLOAT64) and `is_bigendian`. Trailing bytes that do not form a whole sample
/// are ignored. Returns an empty vector for an empty blob or an unrecognized
/// dtype.
std::vector<float> decode_samples(
  const marine_acoustic_msgs::msg::SonarImageData & image);

/// Combine an optional port and starboard row into one centered row.
///
/// The port row is reversed and placed left of nadir, the starboard row right of
/// it, so the result runs port-far -> nadir -> starboard-far. If only one side is
/// present it is returned unchanged; if neither is present the result is empty.
/// `range_max` is the larger of the two; `stamp` is the later of the two.
std::optional<WaterfallRow> combine_rows(
  const std::optional<WaterfallRow> & port,
  const std::optional<WaterfallRow> & starboard);

}  // namespace rqt_sonar_waterfall

#endif  // RQT_SONAR_WATERFALL__WATERFALL_MODEL_HPP_
