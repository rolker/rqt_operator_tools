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

#ifndef RQT_MARINE_CONTROL__RESPONSIVE_HUB_LAYOUT_HPP_
#define RQT_MARINE_CONTROL__RESPONSIVE_HUB_LAYOUT_HPP_

#include <QByteArray>
#include <QWidget>

class QResizeEvent;
class QSplitter;
class QTabWidget;
class QTimer;

namespace qt_gui_cpp
{
class Settings;
}  // namespace qt_gui_cpp

namespace rqt_marine_control
{

class TabManager;

/// Owns the responsive arrangement of the Connections hub and the device
/// QTabWidget. Wide: the hub sits in the left pane of a QSplitter beside the tabs.
/// Narrow: the hub is reparented into the tab widget as a non-closable tab 0 and
/// the splitter collapses to just the tabs. The switch is hysteretic — it flips to
/// wide only above kHubPanelWidthHi and to narrow only below kHubPanelWidthLo, so
/// a drag that hovers near the breakpoint does not thrash the layout. resizeEvent
/// is debounced so a continuous drag reparents at most once it settles.
///
/// The hub widget instance is preserved across the reparent (never rebuilt), so
/// its checkbox state and the marshalled devices-changed callback target survive.
/// The selected device tab is preserved by state-topic key (via TabManager), not
/// by index, because docking the hub as tab 0 shifts every device tab's index.
class ResponsiveHubLayout : public QWidget
{
  Q_OBJECT

public:
  // Breakpoints (compile-time per operator decision 3, issue #97): switch to the
  // side-panel layout only above kHubPanelWidthHi and back to the tab layout only
  // below kHubPanelWidthLo. The gap between them is the hysteresis dead band.
  static constexpr int kHubPanelWidthHi = 860;
  static constexpr int kHubPanelWidthLo = 700;
  // Coalesce a continuous resize drag into a single reparent once it settles.
  static constexpr int kResizeDebounceMs = 200;

  enum class Mode { Wide, Narrow };

  explicit ResponsiveHubLayout(QWidget * parent = nullptr);

  /// The tab widget device tabs live in. Hand this to the TabManager.
  QTabWidget * tabWidget() const {return tabs_;}

  /// Adopt the hub widget and place it per the current mode. Takes no ownership
  /// beyond Qt's parent/child relationship established by the reparent.
  void setHub(QWidget * hub);
  /// Needed to preserve the selected device tab by topic across a reparent.
  void setTabManager(TabManager * tab_manager);

  Mode mode() const {return mode_;}

  /// Apply the hysteresis decision for a given width and reparent if the mode
  /// changes. Called (debounced) from resizeEvent; exposed so tests can drive a
  /// width deterministically without pumping the resize timer.
  void updateForWidth(int width);

  void saveSettings(qt_gui_cpp::Settings & settings) const;
  void restoreSettings(const qt_gui_cpp::Settings & settings);

protected:
  void resizeEvent(QResizeEvent * event) override;

private slots:
  void onResizeTimeout();

private:
  void toWide();
  void toNarrow();

  QSplitter * splitter_ = nullptr;
  QTabWidget * tabs_ = nullptr;
  QWidget * hub_ = nullptr;
  TabManager * tab_manager_ = nullptr;
  QTimer * resize_timer_ = nullptr;
  Mode mode_ = Mode::Wide;
  // Splitter divider position restored from settings, applied when (or if) the
  // wide layout exists (restoreSettings can run while narrow).
  QByteArray restored_splitter_state_;
};

}  // namespace rqt_marine_control

#endif  // RQT_MARINE_CONTROL__RESPONSIVE_HUB_LAYOUT_HPP_
