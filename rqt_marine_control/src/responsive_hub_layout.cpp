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

#include "rqt_marine_control/responsive_hub_layout.hpp"

#include <QList>
#include <QResizeEvent>
#include <QSplitter>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <string>

#include <qt_gui_cpp/settings.h>  // NOLINT(build/include_order)

#include "marine_control_widgets/control_set_widget.hpp"
#include "rqt_marine_control/tab_manager.hpp"

namespace rqt_marine_control
{

ResponsiveHubLayout::ResponsiveHubLayout(QWidget * parent)
: QWidget(parent)
{
  auto * outer = new QVBoxLayout(this);
  outer->setContentsMargins(0, 0, 0, 0);

  splitter_ = new QSplitter(Qt::Horizontal);
  tabs_ = new QTabWidget();
  tabs_->setTabsClosable(true);
  tabs_->setMovable(true);
  // The tabs start as the splitter's only pane; the hub is inserted to its left
  // when the layout goes wide.
  splitter_->addWidget(tabs_);
  outer->addWidget(splitter_);

  resize_timer_ = new QTimer(this);
  resize_timer_->setSingleShot(true);
  connect(resize_timer_, &QTimer::timeout, this, &ResponsiveHubLayout::onResizeTimeout);
}

void ResponsiveHubLayout::setHub(QWidget * hub)
{
  hub_ = hub;
  if (hub_ == nullptr) {
    return;
  }
  // Place per the current mode (default wide). The first settled resize adjusts it
  // to the actual panel width.
  if (mode_ == Mode::Wide) {
    toWide();
  } else {
    toNarrow();
  }
}

void ResponsiveHubLayout::setTabManager(TabManager * tab_manager)
{
  tab_manager_ = tab_manager;
}

void ResponsiveHubLayout::resizeEvent(QResizeEvent * event)
{
  QWidget::resizeEvent(event);
  // Debounce: a continuous drag restarts the one-shot timer, so the reparent runs
  // once the width settles rather than on every intermediate pixel.
  resize_timer_->start(kResizeDebounceMs);
}

void ResponsiveHubLayout::onResizeTimeout()
{
  updateForWidth(width());
}

void ResponsiveHubLayout::updateForWidth(int width)
{
  if (hub_ == nullptr) {
    return;
  }

  Mode target = mode_;
  if (width > kHubPanelWidthHi) {
    target = Mode::Wide;
  } else if (width < kHubPanelWidthLo) {
    target = Mode::Narrow;
  }
  // A width inside [kHubPanelWidthLo, kHubPanelWidthHi] keeps the current mode —
  // this is the hysteresis that prevents thrashing near the breakpoint.
  if (target == mode_) {
    return;
  }

  // Preserve which device tab is selected across the reparent. Docking the hub as
  // tab 0 shifts every device index, so we track the topic and re-select by widget.
  std::string selected_topic;
  if (tab_manager_ != nullptr) {
    selected_topic = tab_manager_->topicForIndex(tabs_->currentIndex());
  }

  mode_ = target;
  if (mode_ == Mode::Wide) {
    toWide();
  } else {
    toNarrow();
  }

  if (tab_manager_ != nullptr && !selected_topic.empty()) {
    if (auto * widget = tab_manager_->widgetFor(selected_topic)) {
      const int index = tabs_->indexOf(widget);
      if (index >= 0) {
        tabs_->setCurrentIndex(index);
      }
    }
  }
}

void ResponsiveHubLayout::toWide()
{
  // Pull the hub out of the tab bar (if docked) and dock it in the splitter's
  // left pane. insertWidget reparents it out of the tab widget.
  const int index = tabs_->indexOf(hub_);
  if (index >= 0) {
    tabs_->removeTab(index);   // does not delete the hub widget
  }
  splitter_->insertWidget(0, hub_);
  hub_->show();
  if (!restored_splitter_state_.isEmpty()) {
    splitter_->restoreState(restored_splitter_state_);
  } else {
    splitter_->setSizes({250, 600});
  }
}

void ResponsiveHubLayout::toNarrow()
{
  // Dock the hub as tab 0; insertTab reparents it out of the splitter. It must not
  // be closable — a tab represents a device, and the hub is neither.
  const int index = tabs_->insertTab(0, hub_, tr("Connections"));
  if (tabs_->tabBar() != nullptr) {
    tabs_->tabBar()->setTabButton(index, QTabBar::RightSide, nullptr);
    tabs_->tabBar()->setTabButton(index, QTabBar::LeftSide, nullptr);
  }
  tabs_->setCurrentIndex(index);
}

void ResponsiveHubLayout::saveSettings(qt_gui_cpp::Settings & settings) const
{
  if (splitter_ != nullptr) {
    settings.setValue("hub_splitter_state", splitter_->saveState());
  }
}

void ResponsiveHubLayout::restoreSettings(const qt_gui_cpp::Settings & settings)
{
  const QByteArray state = settings.value("hub_splitter_state").toByteArray();
  if (state.isEmpty()) {
    return;
  }
  restored_splitter_state_ = state;
  // Apply now only if the wide layout (with the divider) already exists; otherwise
  // toWide() applies it when the layout next goes wide.
  if (mode_ == Mode::Wide && splitter_ != nullptr) {
    splitter_->restoreState(state);
  }
}

}  // namespace rqt_marine_control
