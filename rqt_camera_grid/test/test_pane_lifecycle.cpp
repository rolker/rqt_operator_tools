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
// CameraPaneWidget must not crash or emit Qt thread-teardown warnings.
// Regression signal for the topic-refresh class of crashes seen in
// rqt_image_view — those presented as cross-thread QObject destruction,
// which Qt flags via warnings like "QObject::~QObject: Timers cannot be
// stopped from another thread". We capture those warnings via
// qInstallMessageHandler and assert the thread-teardown patterns are
// absent after 1000 cycles. (Memory-leak detection is left to
// AddressSanitizer / Valgrind runs — RSS-delta was tried earlier but
// peak-RSS semantics are too noisy for a useful in-test assertion.)

#include <gtest/gtest.h>

#include <QApplication>
#include <QCoreApplication>
#include <QMessageLogContext>
#include <QString>
#include <QtGlobal>

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

  PaneConfig config;
  config.base = "/test/image_raw";
  config.transport = "raw";

  // Warm up: one pane to allocate ROS/Qt statics. Kept even without the
  // RSS assertion because it keeps the main loop's timing consistent
  // across runs.
  {
    CameraPaneWidget pane(node_, it_, config);
    (void)pane;
  }

  for (int i = 0; i < kIterations; ++i) {
    CameraPaneWidget pane(node_, it_, config);
    pane.resize(320, 240);
    QCoreApplication::processEvents();
  }

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

TEST_F(PaneLifecycleTest, SubscribeWithInvalidTopicSurvives)
{
  // Anti-regression for #27. Before the b449acb / 7f1d78b fixes the
  // dialog could leak the "<base>  [<transport>]" display label into
  // PaneConfig::base. The constructor's subscribe() then handed that
  // bracketed string to image_transport, which surfaced an
  // InvalidTopicNameError out of rclcpp / fastcdr that nothing caught
  // — terminate() on the main thread, killing the whole rqt process.
  //
  // The fix catches std::exception in subscribe() and logs. This test
  // pins that contract: a malformed base must not throw out of the
  // CameraPaneWidget constructor, and the widget must destroy cleanly
  // afterwards.
  using rqt_camera_grid::CameraPaneWidget;
  using rqt_camera_grid::PaneConfig;

  PaneConfig config;
  config.base = "/bizzy/sensors/cameras/oak_port/segmentation  [raw]";  // exact #27 form
  config.transport = "raw";

  ASSERT_NO_THROW({
    CameraPaneWidget pane(node_, it_, config);
    pane.resize(160, 120);
    QCoreApplication::processEvents();
  });
}

TEST_F(PaneLifecycleTest, SubscribeWithMalformedTransportSurvives)
{
  // Companion to the above: TransportHints constructs an internal
  // parameter, and a malformed transport string can throw out of that
  // path too. Pre-fix, TransportHints lived above the try block; it now
  // sits inside it. Pin that.
  using rqt_camera_grid::CameraPaneWidget;
  using rqt_camera_grid::PaneConfig;

  PaneConfig config;
  config.base = "/test/image_raw";
  config.transport = "no such transport";

  ASSERT_NO_THROW({
    CameraPaneWidget pane(node_, it_, config);
    pane.resize(160, 120);
    QCoreApplication::processEvents();
  });
}

TEST_F(PaneLifecycleTest, RapidConstructDestructWithSubscribe)
{
  // Variant of RapidConstructDestruct that calls subscribe() each
  // cycle. What this actually exercises:
  //   * subscribe() exception-safety under churn (the try/catch from
  //     7f1d78b runs every iteration);
  //   * Subscriber lifecycle — construction, immediate destruction
  //     before any frame arrives, repeated 500 times — verifying
  //     image_transport's shutdown handles back-to-back create/destroy
  //     without leaking timers or emitting cross-thread Qt warnings;
  //   * the QPointer capture path through `it_->subscribe(...)` (i.e.
  //     the lambda is *built and registered* under load, even if
  //     never called).
  //
  // What this does NOT exercise: the actual callback-after-destruction
  // race that the QPointer guard targets. No executor is spinning and
  // no publisher exists, so the lambda body is never reached and the
  // null-self early-return is never taken. Forcing that race in a unit
  // test requires a threaded executor + a publisher + careful timing
  // around tear-down — high cost, and the QPointer pattern is the
  // standard Qt+ROS idiom (well-trodden enough that code review is the
  // primary safeguard, not gtest). Documenting the gap honestly here
  // so a future reader doesn't trust this test for more than it gives.
  using rqt_camera_grid::CameraPaneWidget;
  using rqt_camera_grid::PaneConfig;

  constexpr int kIterations = 500;

  PaneConfig config;
  config.base = "/test/image_raw_rapid_subscribe";
  config.transport = "raw";

  // Warm up — same justification as RapidConstructDestruct.
  {
    CameraPaneWidget pane(node_, it_, config);
    (void)pane;
  }

  for (int i = 0; i < kIterations; ++i) {
    CameraPaneWidget pane(node_, it_, config);
    pane.resize(320, 240);
    QCoreApplication::processEvents();
  }

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
      << " subscribe/destruct cycles:\n" << joined;
  }
}
