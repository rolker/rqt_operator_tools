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

// Offscreen smoke/robustness tests for the EchogramWidget. The widget renders on
// the CPU (QtCharts + QImage), so QT_QPA_PLATFORM=offscreen is enough — no GL
// context required. The point is that the hardening paths (malformed sample_rate,
// non-finite samples, degenerate dB window, bad ping spacing, extreme resize)
// never crash or trip undefined behavior.

#include <gtest/gtest.h>

#include <QApplication>
#include <QCoreApplication>

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

  std::unique_ptr<QApplication> app_;
};

}  // namespace

TEST_F(EchogramWidgetTest, EmptyResizeNoCrash)
{
  EchogramWidget w(nullptr);
  w.resize(320, 240);  // exercises resizeEvent -> adjustPixmap before any ping
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

TEST_F(EchogramWidgetTest, DegenerateDbWindowNoCrash)
{
  EchogramWidget w(nullptr);
  w.resize(320, 240);
  w.setMinimumDB(5.0f);
  w.setMaximumDB(5.0f);  // zero-width dB window must not divide by zero
  w.addPing(makePing({1.0f, 2.0f, 3.0f}));
  SUCCEED();
}

TEST_F(EchogramWidgetTest, BadPingSpacingNoCrash)
{
  EchogramWidget w(nullptr);
  w.resize(320, 240);
  w.addPing(makePing({1.0f, 2.0f, 3.0f}));
  w.setPingSpacing(0.0f);  // corrupted persisted value path; guarded in adjustPixmap
  EXPECT_FLOAT_EQ(w.pingSpacing(), 0.0f);
  SUCCEED();
}

TEST_F(EchogramWidgetTest, SettersRoundTrip)
{
  EchogramWidget w(nullptr);
  w.setMinimumDB(-80.0f);
  w.setMaximumDB(-5.0f);
  w.setPingSpacing(2.5f);
  EXPECT_FLOAT_EQ(w.minimumDB(), -80.0f);
  EXPECT_FLOAT_EQ(w.maximumDB(), -5.0f);
  EXPECT_FLOAT_EQ(w.pingSpacing(), 2.5f);
}
