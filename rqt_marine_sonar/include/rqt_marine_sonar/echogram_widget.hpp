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

#ifndef RQT_MARINE_SONAR__ECHOGRAM_WIDGET_HPP_
#define RQT_MARINE_SONAR__ECHOGRAM_WIDGET_HPP_

#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLWidget>

#include <cstdint>
#include <map>
#include <utility>
#include <vector>

#include <marine_acoustic_msgs/msg/raw_sonar_image.hpp>
#include <rqt_sonar_waterfall/color_map.hpp>
#include <rqt_sonar_waterfall/gpu_color_map.hpp>

namespace rqt_marine_sonar
{

/// Scrolling water-column echogram, rendered on the GPU.
///
/// Each incoming RawSonarImage ping becomes a vertical column; depth runs down
/// the left axis. Pings of any dtype are decoded once on arrival (shared
/// rqt_sonar_waterfall decoder) and depth-binned to a shared bin size. The raw
/// (full-precision) binned samples for the visible depth window are uploaded to
/// an R32F texture and colormapped in the shared marine_colormap fragment shader
/// (GpuColorMap), so value-window / gain / contrast / palette are GPU uniforms /
/// a small LUT — changing them is a redraw, never a CPU recolor. The depth axis,
/// ticks, wheel zoom (Ctrl = fine, cursor-focused) and left-drag pan are drawn /
/// handled by the widget over the GL frame.
class EchogramWidget : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core
{
  Q_OBJECT

public:
  explicit EchogramWidget(QWidget * parent);
  ~EchogramWidget() override;

  bool autoRange() const;
  float blackPoint() const;
  float whitePoint() const;
  float contrast() const;
  int colorMapIndex() const;
  float pingSpacing() const;

  /// The on-screen echogram as a QImage, grabbed from the GL framebuffer. Exposed
  /// so tests can assert on actual rendering. Non-const: it triggers a render.
  QImage echogramImage();

signals:
  void mouseMoved(QPointF position);

public slots:
  void addPing(const marine_acoustic_msgs::msg::RawSonarImage & ping);
  /// Ingest a burst of pings with a single redraw at the end. Cheap on the GPU
  /// path (one texture upload), but still preferred over per-ping addPing() when
  /// draining a queue.
  void addPings(const std::vector<marine_acoustic_msgs::msg::RawSonarImage> & pings);
  /// Auto-range (default on): the palette spans the live buffered data extent.
  /// Turning it off freezes the current extent so the black/white points trim a
  /// stable window.
  void setAutoRange(bool enabled);
  /// Black/white points in [0, 1] (manual mode): normalized positions within the
  /// frozen data extent that map to the bottom / top of the palette.
  void setBlackPoint(float value);
  void setWhitePoint(float value);
  void setContrast(float contrast);
  void setColorMapIndex(int index);

  /// Set the horizontal spacing between pings for display (>= 1 ping per column).
  void setPingSpacing(float spacing);

protected:
  void initializeGL() override;
  void resizeGL(int w, int h) override;
  void paintGL() override;

  void wheelEvent(QWheelEvent * event) override;
  void mouseMoveEvent(QMouseEvent * event) override;
  void mousePressEvent(QMouseEvent * event) override;
  void mouseReleaseEvent(QMouseEvent * event) override;

private:
  /// One ping, decoded once on arrival: geometry + float samples (any dtype),
  /// plus the cached finite-sample value extremes (for the auto-range scan).
  struct DecodedPing
  {
    float min_depth;
    float max_depth;
    float bin_size;
    std::vector<float> samples;
    float value_min;  ///< min finite sample (== value_max when none)
    float value_max;
  };

  /// Decode and buffer one ping without redrawing. Returns true if accepted
  /// (displayable dtype, finite non-degenerate geometry).
  bool ingestPing(const marine_acoustic_msgs::msg::RawSonarImage & ping);

  /// Recompute the shared depth geometry (min/max depth, bin size) across the
  /// buffer. Returns false if the buffer holds no valid geometry. Populates
  /// min_depth_/max_depth_/bin_size_ on success.
  bool recomputeGeometry();

  /// The visible depth window [min, max] in meters from the zoom/pan state,
  /// clamped to the data extent. {0,0} when there is no data.
  std::pair<float, float> visibleDepthWindow() const;

  /// Live value extent [min, max] across all buffered pings' finite samples.
  /// {0, 1} when the buffer is empty.
  std::pair<float, float> dataExtent() const;

  /// The intensity window [lo, hi] fed to the shader: the live data extent when
  /// auto-range is on, else the frozen extent trimmed by the black/white points.
  std::pair<float, float> valueWindow() const;

  /// Pack the visible depth window x buffered pings into the R32F texture. Must
  /// run with the GL context current (called from paintGL when data_dirty_).
  void uploadTexture();

  std::map<int64_t, DecodedPing> pings_;
  int maximum_ping_count_ = 2048;

  // Intensity scaling. Auto-range (default) spans the live data extent; when off
  // the black/white points (normalized [0, 1]) trim the frozen extent. contrast
  // is the gamma shaping inside the window.
  bool auto_range_ = true;
  float black_ = 0.0f;
  float white_ = 1.0f;
  float frozen_min_ = 0.0f;  ///< data extent captured when auto-range turned off
  float frozen_max_ = 1.0f;
  float contrast_ = 1.0f;
  rqt_sonar_waterfall::ColorMapType color_map_type_ =
    rqt_sonar_waterfall::ColorMapType::Grayscale;
  float ping_spacing_ = 1.0f;

  // Shared depth geometry across the buffer (meters), from recomputeGeometry().
  float min_depth_ = 0.0f;
  float max_depth_ = 0.0f;
  float bin_size_ = 0.0f;

  // Depth zoom/pan. depth_zoom_ >= 0.5; depth_offset_ shifts the window.
  float depth_zoom_ = 1.0f;
  float depth_offset_ = 0.0f;

  bool translating_depth_ = false;
  float depth_translation_start_ = 0.0f;
  float depth_offset_start_ = 0.0f;

  // GPU state.
  rqt_sonar_waterfall::GpuColorMap gpu_;
  unsigned int intensity_tex_ = 0;  ///< R32F, width x height = pings x depth-rows
  int tex_rows_ = 0;                ///< intensity-texture height (depth bins) for the ring
  bool gl_ready_ = false;           ///< initializeGL completed
  bool has_data_ = false;           ///< texture holds at least one column
  bool data_dirty_ = true;          ///< buffer/geometry/zoom changed -> re-upload
  bool palette_dirty_ = true;       ///< palette changed -> re-bake LUT

  // Cached visible window of the most recent upload, for the axis overlay.
  float vis_min_depth_ = 0.0f;
  float vis_max_depth_ = 0.0f;
};

}  // namespace rqt_marine_sonar

#endif  // RQT_MARINE_SONAR__ECHOGRAM_WIDGET_HPP_
