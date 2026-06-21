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

// Offscreen smoke/robustness + render tests for the EchogramWidget. The widget
// renders on the GPU (QOpenGLWidget + the shared GpuColorMap), so the
// render-assertion tests need an offscreen GL context (QT_QPA_PLATFORM=offscreen
// + software GL); they self-skip when no context can be created. Intensity
// scaling is auto-range by default (palette spans the live data extent), with a
// manual black/white window when auto-range is off.

#include <gtest/gtest.h>

#include <QApplication>
#include <QCoreApplication>
#include <QImage>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <vector>

#include <marine_acoustic_msgs/msg/raw_sonar_image.hpp>

#include "rqt_marine_sonar/echogram_widget.hpp"

namespace
{

using rqt_marine_sonar::EchogramWidget;

// A FLOAT32 single-beam ping; sample_rate/sound_speed default to a sane geometry
// unless overridden to exercise the degenerate paths.
marine_acoustic_msgs::msg::RawSonarImage makePing(
  const std::vector<float> & samples, float sample_rate = 1000.0f)
{
  marine_acoustic_msgs::msg::RawSonarImage msg;
  msg.ping_info.sound_speed = 1500.0f;
  msg.sample_rate = sample_rate;
  msg.sample0 = 0;
  msg.samples_per_beam = static_cast<uint32_t>(samples.size());
  msg.image.dtype = marine_acoustic_msgs::msg::SonarImageData::DTYPE_FLOAT32;
  msg.image.is_bigendian = false;
  msg.image.data.resize(samples.size() * sizeof(float));
  std::memcpy(msg.image.data.data(), samples.data(), samples.size() * sizeof(float));
  return msg;
}

// Count "amber" pixels (r >> b, the Bronze palette's signature). The depth-axis
// overlay (gray gridlines, light labels) is not amber, so a positive count
// proves colormapped samples actually rendered.
int countAmber(const QImage & img)
{
  int n = 0;
  for (int y = 0; y < img.height(); ++y) {
    for (int x = 0; x < img.width(); ++x) {
      const QRgb px = img.pixel(x, y);
      if (qRed(px) > qBlue(px) + 20) {
        ++n;
      }
    }
  }
  return n;
}

class EchogramWidgetTest : public ::testing::Test
{
protected:
  void SetUp() override
  {
    if (QCoreApplication::instance() == nullptr) {
      static int argc = 1;
      static char arg0[] = "test_echogram_widget";
      static char * argv[] = {arg0, nullptr};
      app_ = std::make_unique<QApplication>(argc, argv);
    }
  }

  // Realize the widget's GL context offscreen, render, and grab the framebuffer.
  // Returns a null/empty QImage when no GL context is available so callers can
  // GTEST_SKIP rather than fail.
  static QImage renderAndGrab(EchogramWidget & w)
  {
    w.show();
    QCoreApplication::processEvents();
    return w.echogramImage();
  }

  std::unique_ptr<QApplication> app_;
};

}  // namespace

TEST_F(EchogramWidgetTest, EmptyResizeNoCrash)
{
  EchogramWidget w(nullptr);
  w.resize(320, 240);
  SUCCEED();
}

TEST_F(EchogramWidgetTest, NormalPingRenders)
{
  EchogramWidget w(nullptr);
  w.resize(320, 240);
  for (int i = 0; i < 8; ++i) {
    w.addPing(makePing({1.0f, 2.0f, 3.0f, 4.0f}));
  }
  SUCCEED();
}

TEST_F(EchogramWidgetTest, MalformedSampleRateNoCrash)
{
  EchogramWidget w(nullptr);
  w.resize(320, 240);
  w.addPing(makePing({1.0f, 2.0f}, /*sample_rate=*/0.0f));  // bin size -> inf
  w.addPing(makePing({1.0f, 2.0f, 3.0f}));                  // a good one after
  SUCCEED();
}

TEST_F(EchogramWidgetTest, NonFiniteSamplesNoCrash)
{
  const float inf = std::numeric_limits<float>::infinity();
  EchogramWidget w(nullptr);
  w.resize(320, 240);
  w.addPing(makePing({inf, -inf, std::nanf(""), 1.0f}));
  SUCCEED();
}

TEST_F(EchogramWidgetTest, ManualZeroWidthWindowNoCrash)
{
  EchogramWidget w(nullptr);
  w.resize(320, 240);
  w.setAutoRange(false);
  w.setBlackPoint(0.5f);
  w.setWhitePoint(0.5f);  // zero-width window must not draw, must not crash
  w.addPing(makePing({1.0f, 2.0f, 3.0f}));
  SUCCEED();
}

TEST_F(EchogramWidgetTest, Uint16PingRendersColormapped)
{
  // Regression for #54: a UINT16 (GCV) ping must produce visible, colormapped
  // pixels. Auto-range (default) scales the palette to the data, so no manual
  // window is needed. Bronze palette -> rendered samples are amber (r >> b).
  EchogramWidget w(nullptr);
  w.resize(320, 240);
  w.setColorMapIndex(1);  // Bronze

  marine_acoustic_msgs::msg::RawSonarImage msg;
  msg.ping_info.sound_speed = 1500.0f;
  msg.sample_rate = 1000.0f;
  msg.sample0 = 0;
  msg.samples_per_beam = 16;
  msg.image.dtype = marine_acoustic_msgs::msg::SonarImageData::DTYPE_UINT16;
  msg.image.is_bigendian = false;
  for (int i = 0; i < 16; ++i) {           // mid-to-high counts, LE
    msg.image.data.push_back(0x00);
    msg.image.data.push_back(static_cast<uint8_t>(0x60 + 4 * i));
  }
  for (int i = 0; i < 8; ++i) {
    msg.header.stamp.nanosec = 1000u * static_cast<uint32_t>(i);
    w.addPing(msg);
  }

  const QImage img = renderAndGrab(w);
  if (img.isNull() || img.width() == 0) {
    GTEST_SKIP() << "offscreen GL context unavailable";
  }
  EXPECT_GT(countAmber(img), 0) << "no colormapped sample pixels rendered";
}

TEST_F(EchogramWidgetTest, RendersDepthGradientNotCollapsedRow)
{
  // Regression for #63: the merged ring-texture GpuColorMap always applies the
  // ring V-mapping. Without an identity set_ring() matching the texture height,
  // the defaults (capacity=1) collapse every screen row to texture-V 0.5 -- one
  // depth bin smeared down the whole column, destroying the depth axis. A water
  // column whose intensity ramps with depth must therefore render a vertical
  // brightness gradient; a collapsed render makes every row identical.
  EchogramWidget w(nullptr);
  w.resize(320, 240);
  w.setColorMapIndex(1);  // Bronze

  std::vector<float> ramp;                 // shallow -> deep intensity ramp
  for (int i = 0; i < 64; ++i) {
    ramp.push_back(static_cast<float>(i));
  }
  for (int p = 0; p < 400; ++p) {          // fill the canvas width with the ramp
    auto msg = makePing(ramp);
    msg.header.stamp.nanosec = 1000u * static_cast<uint32_t>(p + 1);
    w.addPing(msg);
  }

  const QImage img = renderAndGrab(w);
  if (img.isNull() || img.width() == 0) {
    GTEST_SKIP() << "offscreen GL context unavailable";
  }
  // Mean red over a horizontal band, sampled on the RIGHT half to avoid the
  // left-side depth-axis labels.
  auto bandRed = [&img](int y0, int y1) {
    std::int64_t sum = 0;
    std::int64_t n = 0;
    for (int y = y0; y < y1; ++y) {
      for (int x = img.width() / 2; x < img.width(); ++x) {
        sum += qRed(img.pixel(x, y));
        ++n;
      }
    }
    return n ? static_cast<double>(sum) / static_cast<double>(n) : 0.0;
  };
  const int h = img.height();
  const double top = bandRed(0, h / 8);
  const double bottom = bandRed(h - h / 8, h);
  // A real depth gradient: the shallow and deep bands differ substantially. The
  // collapse bug renders one depth bin everywhere, so the two are ~equal.
  EXPECT_GT(std::abs(bottom - top), 15.0)
    << "no depth gradient (top red=" << top << " bottom red=" << bottom
    << "); identity ring not set -> collapsed depth axis (#63)";
}

TEST_F(EchogramWidgetTest, NanGeometryPingDoesNotPoisonRender)
{
  // A ping whose geometry computes to NaN (NaN sound_speed) must be rejected at
  // ingest — buffered alongside good pings it would otherwise pass NaN through
  // the render path.
  EchogramWidget w(nullptr);
  w.resize(320, 240);
  w.setColorMapIndex(1);  // Bronze

  auto good = makePing({5.0f, 6.0f, 7.0f, 8.0f});
  auto bad = makePing({5.0f, 6.0f, 7.0f, 8.0f});
  bad.ping_info.sound_speed = std::nanf("");

  good.header.stamp.nanosec = 1000u;
  w.addPing(good);
  bad.header.stamp.nanosec = 2000u;
  w.addPing(bad);
  good.header.stamp.nanosec = 3000u;
  w.addPing(good);

  const QImage img = renderAndGrab(w);
  if (img.isNull() || img.width() == 0) {
    GTEST_SKIP() << "offscreen GL context unavailable";
  }
  EXPECT_GT(countAmber(img), 0)
    << "good pings must still render after a NaN-geometry ping";
}

TEST_F(EchogramWidgetTest, ManualZeroWidthWindowClearsImage)
{
  // Auto-range renders; switching to a manual zero-width window (black == white)
  // collapses to the placeholder rather than leaving a stale rendering.
  EchogramWidget w(nullptr);
  w.resize(320, 240);
  w.setColorMapIndex(1);  // Bronze
  for (int i = 0; i < 8; ++i) {
    auto ping = makePing({5.0f, 6.0f, 7.0f, 8.0f});
    ping.header.stamp.nanosec = 1000u * static_cast<uint32_t>(i);
    w.addPing(ping);
  }

  const QImage before = renderAndGrab(w);
  if (before.isNull() || before.width() == 0) {
    GTEST_SKIP() << "offscreen GL context unavailable";
  }
  const int amber_before = countAmber(before);
  ASSERT_GT(amber_before, 0) << "precondition: auto-range rendered";

  w.setAutoRange(false);
  w.setBlackPoint(0.0f);
  w.setWhitePoint(0.0f);  // zero-width = nothing to draw
  const QImage after = renderAndGrab(w);
  // Collapse by ~an order of magnitude (a handful of offscreen-FBO grab edge
  // pixels survive; the rendered curtain does not).
  EXPECT_LT(countAmber(after), amber_before / 4)
    << "zero-width window did not clear the rendering";
}

TEST_F(EchogramWidgetTest, AddPingsBatchMixedValidity)
{
  // The batch entry point must accept the good pings and drop the malformed
  // ones, same as the per-ping path.
  EchogramWidget w(nullptr);
  w.resize(320, 240);

  std::vector<marine_acoustic_msgs::msg::RawSonarImage> batch;
  for (int i = 0; i < 4; ++i) {
    auto ping = makePing({1.0f, 2.0f, 3.0f});
    ping.header.stamp.nanosec = 1000u * static_cast<uint32_t>(i);
    batch.push_back(ping);
  }
  batch.push_back(makePing({1.0f, 2.0f}, /*sample_rate=*/0.0f));  // rejected
  w.addPings(batch);
  SUCCEED();
}

TEST_F(EchogramWidgetTest, BadPingSpacingNoCrash)
{
  EchogramWidget w(nullptr);
  w.resize(320, 240);
  w.addPing(makePing({1.0f, 2.0f, 3.0f}));
  w.setPingSpacing(0.0f);  // corrupted persisted value path; clamped in uploadTexture
  EXPECT_FLOAT_EQ(w.pingSpacing(), 0.0f);
  SUCCEED();
}

TEST_F(EchogramWidgetTest, SettersRoundTrip)
{
  EchogramWidget w(nullptr);
  w.setAutoRange(false);
  w.setBlackPoint(0.2f);
  w.setWhitePoint(0.8f);
  w.setContrast(0.7f);
  w.setColorMapIndex(2);
  w.setPingSpacing(2.5f);
  EXPECT_FALSE(w.autoRange());
  EXPECT_FLOAT_EQ(w.blackPoint(), 0.2f);
  EXPECT_FLOAT_EQ(w.whitePoint(), 0.8f);
  EXPECT_FLOAT_EQ(w.contrast(), 0.7f);
  EXPECT_EQ(w.colorMapIndex(), 2);
  EXPECT_FLOAT_EQ(w.pingSpacing(), 2.5f);
}

TEST_F(EchogramWidgetTest, BlackWhitePointsClampToUnit)
{
  EchogramWidget w(nullptr);
  w.setBlackPoint(-0.5f);
  w.setWhitePoint(3.0f);
  EXPECT_FLOAT_EQ(w.blackPoint(), 0.0f);
  EXPECT_FLOAT_EQ(w.whitePoint(), 1.0f);
}
