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

#include "rqt_marine_control/tab_manager.hpp"

#include <QString>
#include <QTabWidget>

#include <memory>
#include <string>
#include <utility>

#include "marine_control_widgets/control_set_widget.hpp"

namespace rqt_marine_control
{

TabManager::TabManager(QTabWidget * tabs, TabTransportFactory factory)
: tabs_(tabs), factory_(std::move(factory))
{
}

TabManager::~TabManager()
{
  clear();
}

marine_control_widgets::ControlSetWidget * TabManager::openTab(const std::string & state_topic)
{
  if (auto it = entries_.find(state_topic); it != entries_.end()) {
    const int idx = tabs_->indexOf(it->second->widget);
    if (idx >= 0) {
      tabs_->setCurrentIndex(idx);
    }
    return it->second->widget;
  }

  auto entry = std::make_shared<TabEntry>();
  entry->state_topic = state_topic;
  entry->widget = new marine_control_widgets::ControlSetWidget();
  // Title with the topic until the first ControlSet's device_name arrives.
  const int idx = tabs_->addTab(entry->widget, QString::fromStdString(state_topic));
  tabs_->setCurrentIndex(idx);

  // An edit in this tab publishes only through this tab's transport. The lambda
  // captures the shared entry (so the transport can't dangle) and uses the
  // widget as the connection context, so it is torn down when the widget is
  // deleted on close.
  QObject::connect(
    entry->widget, &marine_control_widgets::ControlSetWidget::controlChanged,
    entry->widget,
    [entry](const QString & name, const QString & value) {
      if (entry->transport) {
        entry->transport->publishChange(name.toStdString(), value.toStdString());
      }
    });

  // applySet() looks the entry up by topic on the GUI thread; if the tab has
  // since closed it is a safe no-op, so this delivery can never touch a freed
  // entry.
  auto on_set = [this, state_topic](const marine_control_interfaces::msg::ControlSet & set) {
      applySet(state_topic, set);
    };
  entry->transport = factory_(state_topic, on_set);

  entries_.emplace(state_topic, std::move(entry));
  return entries_.at(state_topic)->widget;
}

void TabManager::closeTab(const std::string & state_topic)
{
  auto it = entries_.find(state_topic);
  if (it == entries_.end()) {
    return;
  }
  auto entry = it->second;
  // Drop the transport first so no further state arrives while we tear down.
  entry->transport.reset();
  const int idx = tabs_->indexOf(entry->widget);
  if (idx >= 0) {
    tabs_->removeTab(idx);
  }
  // The delivery dangle-safety contract (see openTab/applySet) assumes
  // QWidget::~QWidget does not re-enter the Qt event loop here — true for normal
  // teardown — so a queued ControlSet delivery cannot be dispatched into this
  // half-destroyed tab while it is being deleted.
  delete entry->widget;       // also disconnects the publish lambda
  entry->widget = nullptr;
  entries_.erase(it);
}

void TabManager::clear()
{
  for (auto & [topic, entry] : entries_) {
    entry->transport.reset();
    if (entry->widget) {
      const int idx = tabs_->indexOf(entry->widget);
      if (idx >= 0) {
        tabs_->removeTab(idx);
      }
      delete entry->widget;
      entry->widget = nullptr;
    }
  }
  entries_.clear();
}

void TabManager::applySet(
  const std::string & state_topic, const marine_control_interfaces::msg::ControlSet & set)
{
  auto it = entries_.find(state_topic);
  if (it == entries_.end()) {
    return;     // tab closed before this delivery ran
  }
  auto & entry = it->second;
  entry->widget->apply(set);
  // Replace the topic-string title with the device name on the first set carrying
  // one, so the tab reads as the device rather than the raw topic.
  if (!entry->titled && !set.device_name.empty()) {
    const int idx = tabs_->indexOf(entry->widget);
    if (idx >= 0) {
      tabs_->setTabText(idx, QString::fromStdString(set.device_name));
    }
    entry->titled = true;
  }
}

bool TabManager::hasTab(const std::string & state_topic) const
{
  return entries_.find(state_topic) != entries_.end();
}

int TabManager::tabCount() const
{
  return static_cast<int>(entries_.size());
}

std::string TabManager::topicForIndex(int index) const
{
  QWidget * w = tabs_->widget(index);
  if (w == nullptr) {
    return {};
  }
  for (const auto & [topic, entry] : entries_) {
    if (entry->widget == w) {
      return topic;
    }
  }
  return {};
}

marine_control_widgets::ControlSetWidget * TabManager::widgetFor(
  const std::string & state_topic) const
{
  auto it = entries_.find(state_topic);
  return it == entries_.end() ? nullptr : it->second->widget;
}

}  // namespace rqt_marine_control
