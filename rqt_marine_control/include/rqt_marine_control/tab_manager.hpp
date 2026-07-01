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

#ifndef RQT_MARINE_CONTROL__TAB_MANAGER_HPP_
#define RQT_MARINE_CONTROL__TAB_MANAGER_HPP_

#include <functional>
#include <map>
#include <memory>
#include <string>

#include <marine_control_interfaces/msg/control_set.hpp>

class QScrollArea;
class QTabWidget;

namespace marine_control_widgets
{
class ControlSetWidget;
}  // namespace marine_control_widgets

namespace rqt_marine_control
{

/// Per-tab ROS transport: owns the state subscription + change publisher backing
/// one device tab. It is abstracted behind this interface so the tab lifecycle
/// (TabManager) is unit-testable without a live ROS node — tests inject a
/// counting fake. Destroying a transport tears down its underlying
/// subscription/publisher.
class TabTransport
{
public:
  virtual ~TabTransport() = default;
  /// Publish a ControlValue(name, value) on this tab's change topic.
  virtual void publishChange(const std::string & name, const std::string & value) = 0;
};

/// Builds the transport for a state topic. `on_set` is invoked, ON THE GUI
/// THREAD, with each received ControlSet (the real factory marshals from the
/// executor thread; a test factory may call it directly).
using TabTransportFactory = std::function<std::shared_ptr<TabTransport>(
      const std::string & state_topic,
      std::function<void(const marine_control_interfaces::msg::ControlSet &)> on_set)>;

/// Owns the one-tab-per-device model: a topic-keyed map of
/// (ControlSetWidget + TabTransport) entries inside a QTabWidget. Opening a tab
/// creates a transport via the injected factory; closing one destroys exactly
/// that transport (no leaked subscriptions) and removes its widget. Each tab
/// publishes only through its own transport, so an edit in one tab never reaches
/// another tab's change topic.
///
/// Threading: the map is touched only on the GUI thread. The single cross-thread
/// point is the transport's ROS callback, which the real transport marshals onto
/// the GUI thread (capturing a self-contained snapshot, never a TabManager/entry
/// pointer) before applySet() runs — so a tab closing concurrently cannot dangle
/// the delivery. No mutex is therefore needed here.
class TabManager
{
public:
  TabManager(QTabWidget * tabs, TabTransportFactory factory);
  ~TabManager();

  /// Open a tab for `state_topic`, or focus it if one already exists. The tab is
  /// titled with the topic string until the first ControlSet's device_name
  /// arrives. Returns the tab's widget (never null).
  marine_control_widgets::ControlSetWidget * openTab(const std::string & state_topic);

  /// Tear down the tab for `state_topic`: destroy its transport (and thus its
  /// subscription/publisher) and remove + delete its widget. No-op if absent.
  void closeTab(const std::string & state_topic);

  /// Tear down every tab (used on plugin shutdown).
  void clear();

  bool hasTab(const std::string & state_topic) const;
  int tabCount() const;
  /// The state topic backing the tab at a QTabWidget index, or "" if out of range.
  std::string topicForIndex(int index) const;
  /// The QTabWidget index of the tab backing `state_topic`, or -1 if none. Prefer
  /// this over indexOf(widgetFor(topic)): each tab's page is a scroll-area wrapper,
  /// not the ControlSetWidget itself, so the widget is not the tab-widget's child.
  int tabIndexFor(const std::string & state_topic) const;
  marine_control_widgets::ControlSetWidget * widgetFor(const std::string & state_topic) const;

private:
  struct TabEntry
  {
    std::string state_topic;
    // The scroll-area page is the tab's widget in the QTabWidget (owned by it); the
    // ControlSetWidget is the page's child (owned by the page). So a tall control
    // set scrolls inside the tab instead of forcing the whole plugin to grow.
    QScrollArea * page = nullptr;
    marine_control_widgets::ControlSetWidget * widget = nullptr;
    std::shared_ptr<TabTransport> transport;
    bool titled = false;   // has device_name been applied to the tab title yet?
  };

  void applySet(
    const std::string & state_topic, const marine_control_interfaces::msg::ControlSet & set);

  QTabWidget * tabs_;
  TabTransportFactory factory_;
  std::map<std::string, std::shared_ptr<TabEntry>> entries_;   // keyed by state topic
};

}  // namespace rqt_marine_control

#endif  // RQT_MARINE_CONTROL__TAB_MANAGER_HPP_
