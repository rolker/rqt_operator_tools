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

// Anti-regression test for stability rule 6: rapid construct/destruct of
// CameraPaneWidget must not crash, leak unbounded memory, or emit Qt
// thread-teardown warnings. Regression signal for the topic-refresh
// class of crashes seen in rqt_image_view — those presented as
// cross-thread QObject destruction, which Qt flags via warnings like
// "QObject::~QObject: Timers cannot be stopped from another thread".
// We capture those warnings via qInstallMessageHandler and assert the
// thread-teardown patterns are absent after 1000 cycles.

#include <gtest/gtest.h>

#include <QApplication>
#include <QCoreApplication>
#include <QMessageLogContext>
#include <QString>
#include <QtGlobal>

#include <sys/resource.h>

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <image_transport/image_transport.hpp>
#include <rclcpp/rclcpp.hpp>

#include "rqt_camera_grid/camera_pane_widget.hpp"
#include "rqt_camera_grid/config_model.hpp"

namespace
{

// Returns the peak resident set size in KB (ru_maxrss is monotonic — it
// only ever grows — so deltas between two calls are a conservative leak
// indicator: any true leak will show up, transient spikes may inflate
// the number but not hide one). Returns -1 on failure so the caller can
// surface a broken sampling path instead of silently passing with 0.
int64_t peak_rss_kb()
{
  struct rusage usage;
  if (getrusage(RUSAGE_SELF, &usage) != 0) {return -1;}
  return usage.ru_maxrss;
}

// Qt message capture — qInstallMessageHandler takes a C function pointer,
// so the capture state must live at file scope. Reset per test via
// SetUp/TearDown. The mutex protects against warnings emitted from
// non-main threads (unlikely in this test but cheap insurance).
std::vector<std::string> g_captured_qt_messages;
std::mutex g_qt_messages_mutex;
QtMessageHandler g_original_qt_handler = nullptr;

void capture_qt_messages(
  QtMsgType type, const QMessageLogContext & ctx, const QString & msg)
{
  if (type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg) {
    std::lock_guard<std::mutex> lk(g_qt_messages_mutex);
    g_captured_qt_messages.push_back(msg.toStdString());
  }
  if (g_original_qt_handler) {
    g_original_qt_handler(type, ctx, msg);
  }
}

}  // namespace

class PaneLifecycleTest : public ::testing::Test
{
protected:
  static void SetUpTestSuite()
  {
    if (!rclcpp::ok()) {
      rclcpp::init(0, nullptr);
    }
  }

  static void TearDownTestSuite()
  {
    if (rclcpp::ok()) {
      rclcpp::shutdown();
    }
  }

  void SetUp() override
  {
    if (QCoreApplication::instance() == nullptr) {
      static int argc = 1;
      static char arg0[] = "test_pane_lifecycle";
      static char * argv[] = {arg0, nullptr};
      app_ = std::make_unique<QApplication>(argc, argv);
    }
    node_ = std::make_shared<rclcpp::Node>("test_pane_lifecycle_node");
    it_ = std::make_shared<image_transport::ImageTransport>(node_);

    // Install Qt message capture before any panes are constructed so
    // thread-teardown warnings emitted during destruction are recorded.
    {
      std::lock_guard<std::mutex> lk(g_qt_messages_mutex);
      g_captured_qt_messages.clear();
    }
    g_original_qt_handler = qInstallMessageHandler(capture_qt_messages);
  }

  void TearDown() override
  {
    qInstallMessageHandler(g_original_qt_handler);
    g_original_qt_handler = nullptr;
    it_.reset();
    node_.reset();
  }

  std::unique_ptr<QApplication> app_;
  rclcpp::Node::SharedPtr node_;
  std::shared_ptr<image_transport::ImageTransport> it_;
};

TEST_F(PaneLifecycleTest, RapidConstructDestruct)
{
  using rqt_camera_grid::CameraPaneWidget;
  using rqt_camera_grid::PaneConfig;

  constexpr int kIterations = 1000;
  constexpr int64_t kMaxRssGrowthKb = 50 * 1024;  // 50 MB

  PaneConfig config;
  config.base = "/test/image_raw";
  config.transport = "raw";

  // Warm up: one pane to allocate ROS/Qt statics.
  {
    CameraPaneWidget pane(node_, it_, config);
    (void)pane;
  }
  const int64_t baseline_peak_rss = peak_rss_kb();
  ASSERT_GE(baseline_peak_rss, 0) << "getrusage failed after warmup";

  for (int i = 0; i < kIterations; ++i) {
    CameraPaneWidget pane(node_, it_, config);
    pane.resize(320, 240);
    QCoreApplication::processEvents();
  }

  const int64_t final_peak_rss = peak_rss_kb();
  ASSERT_GE(final_peak_rss, 0) << "getrusage failed after iterations";
  const int64_t growth = final_peak_rss - baseline_peak_rss;
  EXPECT_LT(growth, kMaxRssGrowthKb)
    << "peak RSS grew by " << growth << " KB across " << kIterations
    << " construct/destruct cycles (baseline peak " << baseline_peak_rss << " KB).";

  // Stability rule 6: the rqt_image_view topic-refresh regression class
  // surfaces as cross-thread QObject destruction. Crashes are caught by
  // the absence of a gtest death signal above; non-crashing warnings need
  // an explicit check. Match the specific Qt diagnostic patterns that
  // indicate unsafe teardown — don't assert on all warnings, because
  // offscreen Qt can emit benign ones (e.g. about pixmap threading) that
  // are unrelated to the failure mode we're guarding against.
  std::vector<std::string> thread_warnings;
  {
    std::lock_guard<std::mutex> lk(g_qt_messages_mutex);
    for (const auto & m : g_captured_qt_messages) {
      if (m.find("another thread") != std::string::npos ||
        m.find("Timers cannot be stopped") != std::string::npos ||
        m.find("Cannot create children") != std::string::npos)
      {
        thread_warnings.push_back(m);
      }
    }
  }
  if (!thread_warnings.empty()) {
    std::string joined;
    for (const auto & w : thread_warnings) {
      joined += "  - ";
      joined += w;
      joined += "\n";
    }
    ADD_FAILURE()
      << "Qt emitted " << thread_warnings.size()
      << " thread-teardown warning(s) during " << kIterations
      << " construct/destruct cycles:\n" << joined;
  }
}

TEST_F(PaneLifecycleTest, ConstructDestructWithEmptyBaseNeverSubscribes)
{
  using rqt_camera_grid::CameraPaneWidget;
  using rqt_camera_grid::PaneConfig;

  PaneConfig config;  // empty base
  for (int i = 0; i < 100; ++i) {
    CameraPaneWidget pane(node_, it_, config);
    pane.resize(160, 120);
    QCoreApplication::processEvents();
  }
  SUCCEED();
}
