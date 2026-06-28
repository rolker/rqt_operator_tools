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

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <builtin_interfaces/msg/time.hpp>
#include <geometry_msgs/msg/transform.hpp>
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
  /// port-far -> nadir -> starboard-far (see combine_rows()). `nadir_index`
  /// marks the split: indices [0, nadir_index) are the port side stored
  /// reversed (so [nadir_index-1] is port-nadir, [0] is port-far) and indices
  /// [nadir_index, size()) are the starboard side in natural order (nadir->far).
  std::vector<float> intensities;

  /// Maximum slant range represented, in meters. 0 if unknown. For a combined
  /// row this is the larger of the two sides (used by the slant-range label).
  double range_max = 0.0;

  /// Per-side maximum slant range, in meters; 0 if that side is absent/unknown.
  /// Carried separately so an asymmetric or single-sided ping projects each side
  /// against its own range rather than a shared max.
  double range_max_port = 0.0;
  double range_max_stbd = 0.0;

  /// Index in `intensities` of the port/starboard split (the centered nadir).
  /// Equals the port sample count: port-only -> size() (all left of nadir),
  /// starboard-only -> 0 (all right of nadir).
  std::size_t nadir_index = 0;

  /// Sonar altitude above the bottom (slant range to the first bottom return at
  /// nadir), in meters; 0 if unknown. Drives water-column removal + slant->ground
  /// conversion. Stamped from the depth subscription at row assembly.
  double altitude = 0.0;

  /// Acquisition time in seconds since the epoch. 0 if unknown. Used for the
  /// Contact stamp and ordering; for the TF pose lookup prefer `stamp_time`,
  /// which avoids a double round-trip on the nanosecond field.
  double stamp = 0.0;

  /// Original message header stamp (sec/nanosec), carried unflattened so the
  /// earth<-sensor TF lookup uses the exact ping time rather than re-splitting
  /// the `stamp` double (issue #86). Zero-initialised when unknown.
  builtin_interfaces::msg::Time stamp_time;

  /// TF frame the ping was observed in (RawSonarImage.header.frame_id, i.e. the
  /// sidescan sensor frame). Source frame for the earth<-sensor pose lookup that
  /// georeferences a marked target (issue #86). Empty when unknown.
  std::string sensor_frame;

  /// earth (REP-105 ECEF) <- sensor_frame transform at this ping's stamp,
  /// resolved from TF in the plugin's post_row(). The translation is the sensor
  /// origin in ECEF metres; the rotation maps sensor axes into ECEF. Valid only
  /// when has_pose is true.
  geometry_msgs::msg::Transform sensor_to_earth;

  /// Whether sensor_to_earth was resolved (a TF hit). A row with has_pose=false
  /// still displays, but is un-markable (no georeference is available).
  bool has_pose = false;

  /// Cached intensity extremes over `intensities`, populated once when the row
  /// enters the display buffer. They let auto-range scan two numbers per row
  /// (O(rows)) instead of every sample (O(rows x samples)) on each ping.
  /// `has_intensity_range` is false until computed, and for an empty row.
  float min_intensity = 0.0f;
  float max_intensity = 0.0f;
  bool has_intensity_range = false;

  /// TVG-corrected copy of `intensities` (same layout), precomputed once per
  /// ping so toggling display TVG is a buffer swap, not a recompute. Populated
  /// lazily — only once TVG is enabled. `tvg_slope` records the slope it was
  /// computed at so a slope change can detect staleness. `min/max_intensity_tvg`
  /// mirror the raw extremes for auto-range over the corrected values.
  std::vector<float> intensities_tvg;
  float min_intensity_tvg = 0.0f;
  float max_intensity_tvg = 0.0f;
  bool has_tvg = false;
  double tvg_slope = 0.0;
};

/// Decode a SonarImageData blob into per-sample float values.
///
/// Honors `dtype` (UINT8/INT8/UINT16/INT16/UINT32/INT32/UINT64/INT64/FLOAT32/
/// FLOAT64) and `is_bigendian`. Trailing bytes that do not form a whole sample
/// are ignored. Returns an empty vector for an empty blob or an unrecognized
/// dtype.
std::vector<float> decode_samples(
  const marine_acoustic_msgs::msg::SonarImageData & image);

/// Full-scale intensity for a SonarImageData `dtype`, for seeding the manual
/// range control so its default matches the source's bit depth.
///
/// Returns the maximum positive value of the integer types
/// (UINT8 -> 255, INT8 -> 127, UINT16 -> 65535, INT16 -> 32767), clamped to
/// 1e9 for 32-/64-bit integers (the manual-range spin box maxes at 1e9; use
/// auto-range for data this wide). Float types return 1.0 and unrecognized
/// dtypes return 65535.0 — neither has a meaningful fixed full scale, so
/// auto-range is the right tool there and the value is only a starting point
/// the operator can override. Kept separate from `decode_samples` because the
/// manual-range default needs the dtype semantics without decoding a payload.
///
/// Takes the full `uint32` dtype (matching SonarImageData::dtype) so an
/// out-of-range value falls through to the same default branch as
/// `decode_samples`, rather than aliasing a valid case via narrowing.
double default_full_scale(uint32_t dtype);

/// Combine an optional port and starboard row into one centered row.
///
/// The port row is reversed and placed left of nadir, the starboard row right of
/// it, so the result runs port-far -> nadir -> starboard-far. A single side is
/// laid out the same way so nadir is always at `nadir_index` (port-only -> all
/// samples left of nadir; starboard-only -> all right) — the renderer centers
/// nadir even when one side is silent. Neither present -> empty. `range_max` is
/// the larger of the two (for the slant label); `range_max_port`/`range_max_stbd`
/// carry each side's range; `stamp` is the later of the two. The per-ping pose
/// metadata (`sensor_frame`/`sensor_to_earth`/`has_pose`) is propagated from the
/// present side (port preferred) so a combined row stays markable downstream.
std::optional<WaterfallRow> combine_rows(
  const std::optional<WaterfallRow> & port,
  const std::optional<WaterfallRow> & starboard);

/// Horizontal (ground) range for a slant range given the sonar altitude:
/// sqrt(max(slant^2 - altitude^2, 0)). With `altitude <= 0` this is just the
/// slant range (no water column to remove).
double ground_range(double slant_range, double altitude);

/// Project one across-track row onto a centered display axis, removing the water
/// column and converting slant->ground when `ground` is set.
///
/// Output has `columns` samples with nadir at the center column. For each output
/// column at signed display range `d` (negative = port/left, positive =
/// starboard/right, axis unit = meters, spanning [-half_width, +half_width]):
/// pick the side (and its slant range `range_port`/`range_stbd`; a 0 range or a
/// column beyond the side's reach yields 0 = no data), convert `d` to slant
/// (`ground` ? sqrt(d^2 + altitude^2) : |d|), and nearest-sample it within that
/// side's sub-array (delimited by `nadir_index`). Pure; unit-tested.
std::vector<float> project_row(
  const std::vector<float> & samples, std::size_t nadir_index,
  double range_port, double range_stbd, double altitude, bool ground,
  double half_width, std::size_t columns);

/// In-place form of project_row(): writes `columns` floats to `out` (every
/// column set, 0 = no data) instead of allocating a vector. Lets the renderer
/// project straight into the texture staging buffer, one row per repaint, with
/// no per-row heap allocation. `out` must have room for `columns` floats.
void project_row_into(
  float * out, const std::vector<float> & samples, std::size_t nadir_index,
  double range_port, double range_stbd, double altitude, bool ground,
  double half_width, std::size_t columns);

/// TVG-correct a row in place-shape: multiply each sample by
/// (max(R, ref_range) / ref_range)^slope, where R is that sample's slant range
/// derived from its position and the side's range (`range_port` for the reversed
/// port sub-array [0, nadir_index), `range_stbd` for [nadir_index, size())).
/// `slope <= 0` returns an identity copy. A side whose range is 0 (unknown) is
/// copied unchanged (no slant to scale by). Result has the same size/layout.
std::vector<float> apply_tvg(
  const std::vector<float> & samples, std::size_t nadir_index,
  double range_port, double range_stbd, double slope, double ref_range = 1.0);

}  // namespace rqt_sonar_waterfall

#endif  // RQT_SONAR_WATERFALL__WATERFALL_MODEL_HPP_
