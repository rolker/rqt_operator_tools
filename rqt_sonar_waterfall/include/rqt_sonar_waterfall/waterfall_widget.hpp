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
  /// Min/max intensity of a single row (empty -> {0, 1}).
  static std::pair<float, float> row_min_max(const WaterfallRow & row);
  /// Intensity range fed to the shader: manual range, or the buffer auto-range.
  std::pair<float, float> active_range() const;

  WaterfallBuffer buffer_;
  GpuColorMap gpu_;

  unsigned int intensity_tex_ = 0;  ///< R32F, width x height = max-samples x rows
  bool has_data_ = false;           ///< intensity_tex_ holds at least one row
  bool gl_ready_ = false;           ///< initializeGL completed
  bool data_dirty_ = false;         ///< buffer changed -> re-upload in paintGL
  bool palette_dirty_ = false;      ///< color map changed -> re-bake LUT in paintGL

  double range_max_ = 0.0;  ///< slant range of the newest row, meters (0 = unknown)

  ColorMapType color_map_type_ = ColorMapType::Grayscale;
  float gain_ = 1.0f;
  float contrast_ = 1.0f;
  bool frozen_ = false;
  bool auto_range_ = true;
  float manual_min_ = 0.0f;
  float manual_max_ = 1.0f;
};

}  // namespace rqt_sonar_waterfall

#endif  // RQT_SONAR_WATERFALL__WATERFALL_WIDGET_HPP_
