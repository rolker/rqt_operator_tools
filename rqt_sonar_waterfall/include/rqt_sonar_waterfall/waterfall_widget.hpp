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

#ifndef RQT_SONAR_WATERFALL__WATERFALL_WIDGET_HPP_
#define RQT_SONAR_WATERFALL__WATERFALL_WIDGET_HPP_

#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLWidget>

#include <cstddef>
#include <utility>

#include "rqt_sonar_waterfall/color_map.hpp"
#include "rqt_sonar_waterfall/gpu_color_map.hpp"
#include "rqt_sonar_waterfall/waterfall_buffer.hpp"
#include "rqt_sonar_waterfall/waterfall_model.hpp"

namespace rqt_sonar_waterfall
{

/// Scrolling backscatter waterfall canvas, rendered on the GPU.
///
/// Geometry-agnostic: it consumes already-assembled WaterfallRows (newest drawn
/// at the top, older rows scroll downward). The raw (full-precision) intensities
/// are uploaded to an R32F texture and colormapped in a fragment shader via the
/// shared marine_colormap GLSL (see GpuColorMap), so intensity range, gain,
/// contrast and palette are GPU uniforms / a small LUT — changing them is a
/// redraw, never a CPU recolor, and the data is not quantized to 8 bits.
///
/// The public API matches the former CPU widget so the control panel / plugin
/// are unchanged. GL work happens only in initializeGL/paintGL (context current);
/// the setters just stage state and call update().
class WaterfallWidget : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core
{
  Q_OBJECT

public:
  explicit WaterfallWidget(QWidget * parent = nullptr);
  ~WaterfallWidget() override;

  /// Append the newest row. No-op while frozen.
  ///
  /// GUI-thread only: this touches `buffer_` and the GL dirty flags with no
  /// locking. ROS subscription callbacks run on the executor thread and MUST
  /// marshal to the GUI thread (the plugin uses a queued `QMetaObject::invokeMethod`)
  /// before calling this. Calling it directly from another thread races.
  void add_row(const WaterfallRow & row);

  /// Drop all buffered rows.
  void clear();

  // --- client-side view controls ---
  void set_color_map(ColorMapType type);
  void set_gain(float gain);
  void set_contrast(float contrast);
  /// Scrollback depth (rows); clamped to >= 1.
  void set_history(std::size_t rows);
  /// When frozen, add_row() is ignored so the current view holds still.
  void set_frozen(bool frozen);
  /// Auto-scale intensity to the buffered min/max (the default).
  void set_auto_range(bool enabled);
  /// Fixed intensity range used when auto-range is disabled.
  void set_manual_range(float min, float max);

  // --- geometry / correction controls (issue #58) ---
  /// Remove the water column and convert slant->ground range (needs altitude).
  void set_ground_range(bool enabled);
  /// Render all visible pings at one metres-per-pixel scale (auto-fit widest).
  void set_uniform_scale(bool enabled);
  /// Draw across-track range gridlines + labels.
  void set_range_lines(bool enabled);
  /// Scale the range-line count (>0; higher = more lines). Clamped to a sane range.
  void set_range_line_density(float density);
  /// Display TVG correction, using the precomputed per-row corrected samples.
  void set_tvg(bool enabled);
  /// TVG slope exponent (R/ref)^slope; <=0 is identity. Re-derives the cache.
  void set_tvg_slope(float slope);

  bool frozen() const {return frozen_;}
  std::size_t history() const {return buffer_.capacity();}

protected:
  void initializeGL() override;
  void resizeGL(int w, int h) override;
  void paintGL() override;

private:
  /// (Re)upload the whole buffer to the intensity texture. Must run with the GL
  /// context current (called from paintGL when data_dirty_). Resamples each row
  /// to the buffer's max width; rows are stored oldest-first so screen-top maps
  /// to the newest row without a V flip.
  void upload_texture();
  /// Fast path: upload the `count` newest rows incrementally into the ring
  /// texture via glTexSubImage2D (O(count * width)), avoiding a full re-projection
  /// and re-upload. Falls back to upload_texture() when the texture geometry
  /// (width / uniform scale / capacity) would change. GL context must be current.
  void append_rows(std::size_t count);
  /// Min/max intensity of a single row (empty -> {0, 1}).
  static std::pair<float, float> row_min_max(const WaterfallRow & row);
  /// Intensity range fed to the shader: manual range, or the buffer auto-range.
  std::pair<float, float> active_range() const;
  /// Auto-range over the buffered rows' active (raw or TVG) cached extremes.
  std::pair<float, float> buffer_auto_range() const;
  /// Populate/refresh each buffered row's TVG cache for the current slope.
  void ensure_tvg_cache();
  /// Compute one row's TVG-corrected samples + extremes for the current slope.
  void compute_row_tvg(WaterfallRow & row) const;

  WaterfallBuffer buffer_;
  GpuColorMap gpu_;

  unsigned int intensity_tex_ = 0;  ///< R32F ring, width x capacity (max-samples x rows)
  bool has_data_ = false;           ///< intensity_tex_ holds at least one row
  bool gl_ready_ = false;           ///< initializeGL completed
  bool data_dirty_ = false;         ///< structural change -> full re-upload in paintGL
  bool palette_dirty_ = false;      ///< color map changed -> re-bake LUT in paintGL

  // --- ring-buffer texture state ---
  // The intensity texture is a fixed `tex_capacity_`-tall ring of `tex_width_`
  // columns. New rows are written at ring_write_ (wrapping); ring_filled_ valid
  // rows trail behind it. add_row() only bumps pending_appends_ so a burst of
  // pings between repaints is one incremental upload, not a full rebuild each.
  std::size_t pending_appends_ = 0;   ///< rows appended since the last upload
  int tex_capacity_ = 0;              ///< allocated texture height (ring length)
  int tex_width_ = 0;                 ///< allocated texture width (columns)
  std::size_t ring_write_ = 0;        ///< next ring row to write
  std::size_t ring_filled_ = 0;       ///< valid rows currently in the ring
  double last_uniform_half_ = -1.0;   ///< uniform half-width baked into the texture

  double range_max_ = 0.0;  ///< slant range of the newest row, meters (0 = unknown)

  ColorMapType color_map_type_ = ColorMapType::Grayscale;
  float gain_ = 1.0f;
  float contrast_ = 1.0f;
  bool frozen_ = false;
  bool auto_range_ = true;
  float manual_min_ = 0.0f;
  float manual_max_ = 1.0f;

  // --- geometry / correction state (issue #58) ---
  bool ground_range_ = true;       ///< water-column removal + slant->ground
  bool uniform_scale_ = true;      ///< one scale for all visible pings
  bool range_lines_ = true;        ///< draw across-track range gridlines
  float range_line_density_ = 1.0f;  ///< multiplies the auto line count
  bool tvg_ = false;               ///< apply display TVG (default off)
  float tvg_slope_ = 1.5f;         ///< TVG exponent when enabled

  /// Half-width and axis nature of the most recent render, for the overlay.
  double display_half_width_ = 0.0;  ///< meters (or samples if non-metric)
  bool display_is_ground_ = false;   ///< axis is ground range
  bool display_metric_ = false;      ///< axis is metres (label with "m")
  bool depth_missing_ = false;       ///< ground requested but newest row has no altitude
};

}  // namespace rqt_sonar_waterfall

#endif  // RQT_SONAR_WATERFALL__WATERFALL_WIDGET_HPP_
