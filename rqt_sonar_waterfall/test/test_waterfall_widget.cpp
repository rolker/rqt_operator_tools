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

// Offscreen-GL render test for the GPU WaterfallWidget: it must render buffered
// rows without crashing, show the colormap gradient, hold still while frozen,
// and reset on clear. Rendering is read back with QOpenGLWidget::grabFramebuffer,
// so a real GL context is required; when none can be created (headless runner
// without software GL) every test self-skips rather than failing the build.

#include <gtest/gtest.h>

#include <QApplication>
#include <QColor>
#include <QCoreApplication>
#include <QImage>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QSurfaceFormat>

#include <cstddef>
#include <memory>

#include "rqt_sonar_waterfall/color_map.hpp"
#include "rqt_sonar_waterfall/waterfall_widget.hpp"

namespace
{

using rqt_sonar_waterfall::WaterfallRow;
using rqt_sonar_waterfall::WaterfallWidget;

// A ramp row (0..width-1) so auto-range spans a real interval: after
// normalization the left edge maps dark, the right edge bright.
WaterfallRow ramp_row(std::size_t width)
{
  WaterfallRow r;
  r.intensities.reserve(width);
  for (std::size_t i = 0; i < width; ++i) {
    r.intensities.push_back(static_cast<float>(i));
  }
  r.range_max = 50.0;
  return r;
}

// True if a 3.3 GL context can be created in this environment.
bool gl_available()
{
  QSurfaceFormat fmt;
  fmt.setRenderableType(QSurfaceFormat::OpenGL);
  fmt.setVersion(3, 3);
  QOffscreenSurface surface;
  surface.setFormat(fmt);
  surface.create();
  if (!surface.isValid()) {
    return false;
  }
  QOpenGLContext ctx;
  ctx.setFormat(fmt);
  if (!ctx.create() || !ctx.makeCurrent(&surface)) {
    return false;
  }
  const bool ok = ctx.format().majorVersion() >= 3;
  ctx.doneCurrent();
  return ok;
}

QImage render(WaterfallWidget & w)
{
  w.resize(64, 64);
  return w.grabFramebuffer();
}

class WaterfallWidgetTest : public ::testing::Test
{
protected:
  void SetUp() override
  {
    if (QCoreApplication::instance() == nullptr) {
      static int argc = 1;
      static char arg0[] = "test_waterfall_widget";
      static char * argv[] = {arg0, nullptr};
      app_ = std::make_unique<QApplication>(argc, argv);
    }
    if (!gl_available()) {
      GTEST_SKIP() << "No OpenGL 3.3 context available (headless without software GL).";
    }
  }

  std::unique_ptr<QApplication> app_;
};

}  // namespace

TEST_F(WaterfallWidgetTest, EmptyRendersWithoutCrash)
{
  WaterfallWidget w;
  QImage img = render(w);
  EXPECT_FALSE(img.isNull());
}

TEST_F(WaterfallWidgetTest, BufferedRowsDrawContent)
{
  WaterfallWidget w;
  w.set_color_map(rqt_sonar_waterfall::ColorMapType::Grayscale);
  for (int i = 0; i < 8; ++i) {
    w.add_row(ramp_row(128));
  }
  QImage img = render(w);
  ASSERT_FALSE(img.isNull());

  // The grayscale ramp must render a left-to-right brightness gradient. Sample at
  // mid-height to stay clear of the top-corner range labels (which are also
  // light) so this verifies the GL render, not the QPainter overlay.
  const int mid = img.height() / 2;
  const int bright = img.pixelColor(img.width() - 2, mid).red();
  const int dark = img.pixelColor(1, mid).red();
  EXPECT_GT(bright, 200);
  EXPECT_LT(dark, 60);
}

TEST_F(WaterfallWidgetTest, FreezeHoldsTheView)
{
  WaterfallWidget w;
  w.add_row(ramp_row(64));
  QImage before = render(w);

  w.set_frozen(true);
  EXPECT_TRUE(w.frozen());
  for (int i = 0; i < 5; ++i) {
    w.add_row(ramp_row(200));  // wider, different rows — must be ignored
  }
  QImage after = render(w);

  EXPECT_EQ(before, after);
}

TEST_F(WaterfallWidgetTest, ClearResetsToPlaceholder)
{
  WaterfallWidget w;
  for (int i = 0; i < 4; ++i) {
    w.add_row(ramp_row(64));
  }
  w.clear();
  QImage img = render(w);
  ASSERT_FALSE(img.isNull());

  // Corner pixel falls on the dark placeholder fill, not on waterfall data.
  const QColor corner = img.pixelColor(1, 1);
  EXPECT_LT(corner.red(), 60);
  EXPECT_LT(corner.green(), 60);
  EXPECT_LT(corner.blue(), 60);
}

TEST_F(WaterfallWidgetTest, HistoryCapacityClamped)
{
  WaterfallWidget w;
  w.set_history(0);
  EXPECT_EQ(w.history(), 1u);
  w.set_history(300);
  EXPECT_EQ(w.history(), 300u);
}
