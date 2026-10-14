"""Glance and detail views of survey-area coverage and quality.

The widget subscribes to the scorer's latched ``coverage_assessment`` topic,
keeps the latest assessment per area (``score_model.Store``) and renders it as
either big room-readable numbers (glance) or a per-area table with per-source
credit (detail).  ROS callbacks only emit a Qt signal; every store and widget
access happens on the Qt thread.
"""

import dataclasses
import html
import importlib
import time
from typing import List, Optional

from python_qt_binding.QtCore import Qt, QTimer, Signal
from python_qt_binding.QtGui import QColor, QFont, QPalette
from python_qt_binding.QtWidgets import (
    QFrame,
    QHBoxLayout,
    QLabel,
    QMenu,
    QPushButton,
    QSizePolicy,
    QTextBrowser,
    QToolButton,
    QVBoxLayout,
    QWidget,
)
from rclpy.qos import (
    DurabilityPolicy,
    HistoryPolicy,
    QoSProfile,
    ReliabilityPolicy,
)

from .score_model import (
    AGING,
    VIEW_DETAIL,
    VIEW_GLANCE,
    AreaScore,
    CoverageConfig,
    Store,
    StoredArea,
    aging_state,
    combined_score,
    format_age,
    format_pct,
    format_score,
    fraction,
    pool,
)

_BACKGROUND = QColor(30, 30, 30)
_TEXT = '#f0f0f0'
_DIM = '#808080'
_COVERED = '#5ab0ff'
_SPEC = '#5ed68a'
_COMBINED = '#f0c050'
_AMBER = '#ffb000'

# The scorer publishes reliable + transient_local with a fixed history of 100
# (see the coverage_assessment README).  A volatile subscription would never
# receive the latched areas, and a best_effort one cannot match at all.
ASSESSMENT_QOS = QoSProfile(
    reliability=ReliabilityPolicy.RELIABLE,
    durability=DurabilityPolicy.TRANSIENT_LOCAL,
    history=HistoryPolicy.KEEP_LAST,
    depth=100,
)


def load_assessment_class():
    """Import ``CoverageAssessment``; ``None`` when marine_interfaces lacks it."""
    try:
        return importlib.import_module('marine_interfaces.msg').CoverageAssessment
    except (ImportError, AttributeError):
        return None


class GlanceTile(QFrame):
    """One area (or the pooled roll-up): title, big numbers, a status line."""

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setFrameShape(QFrame.StyledPanel)
        layout = QVBoxLayout(self)
        layout.setContentsMargins(8, 4, 8, 4)
        layout.setSpacing(2)

        self.title_label = QLabel()
        self.title_label.setAlignment(Qt.AlignCenter)
        layout.addWidget(self.title_label)

        row = QHBoxLayout()
        row.setSpacing(12)
        self.covered_value, covered_col = self._column('covered', _COVERED)
        self.spec_value, spec_col = self._column('to standard', _SPEC)
        self.combined_value, self.combined_col = self._column('combined', _COMBINED)
        row.addLayout(covered_col, 1)
        row.addLayout(spec_col, 1)
        row.addLayout(self.combined_col, 1)
        layout.addLayout(row, 1)

        self.status_label = QLabel()
        self.status_label.setAlignment(Qt.AlignCenter)
        layout.addWidget(self.status_label)

        self._captions = []
        self._show_combined(False)

    def _column(self, caption, color):
        col = QVBoxLayout()
        value = QLabel('--')
        value.setAlignment(Qt.AlignCenter)
        value.setSizePolicy(QSizePolicy.Ignored, QSizePolicy.Expanding)
        value.setProperty('color', color)
        cap = QLabel(caption)
        cap.setAlignment(Qt.AlignCenter)
        cap.setSizePolicy(QSizePolicy.Ignored, QSizePolicy.Fixed)
        col.addWidget(value, 1)
        col.addWidget(cap)
        value._caption = cap
        return value, col

    def _show_combined(self, visible: bool):
        self.combined_value.setVisible(visible)
        self.combined_value._caption.setVisible(visible)

    def set_content(self, title, covered, spec, combined, status, status_color,
                    aging):
        """Show one area; pass ``combined=None`` to hide the third number."""
        self.title_label.setText(title)
        self.covered_value.setText(covered)
        self.spec_value.setText(spec)
        self._show_combined(combined is not None)
        if combined is not None:
            self.combined_value.setText(combined)
        self.status_label.setText(status)
        self._style(status_color, aging)

    def _style(self, status_color, aging):
        dim = _DIM if aging else None
        for value in (self.covered_value, self.spec_value, self.combined_value):
            color = dim or value.property('color')
            value.setStyleSheet(f'color: {color}; font-weight: bold;')
            value._caption.setStyleSheet(f'color: {_DIM};')
        self.title_label.setStyleSheet(
            f'color: {dim or _TEXT}; font-weight: bold;')
        self.status_label.setStyleSheet(f'color: {status_color};')

    def set_pixel_sizes(self, big_px, small_px):
        big = QFont(self.font())
        big.setPixelSize(max(int(big_px), 8))
        small = QFont(self.font())
        small.setPixelSize(max(int(small_px), 8))
        for value in (self.covered_value, self.spec_value, self.combined_value):
            value.setFont(big)
            value._caption.setFont(small)
        self.title_label.setFont(small)
        self.status_label.setFont(small)


class CoverageWidget(QWidget):
    """Container: toolbar plus a glance panel or a detail browser."""

    # (subscription generation, message): ROS thread -> Qt thread hand-off.
    _msg_received = Signal(int, object)

    def __init__(self, node, parent=None, clock=time.monotonic):
        super().__init__(parent)
        self._node = node
        self._clock = clock
        self._config = CoverageConfig()
        self._store = Store()
        self._subscription = None
        self._generation = 0
        self._tiles: List[GlanceTile] = []
        self._tile_keys: Optional[tuple] = None
        self._msg_class = load_assessment_class()

        self.setAutoFillBackground(True)
        palette = self.palette()
        palette.setColor(QPalette.Window, _BACKGROUND)
        self.setPalette(palette)

        self._build_ui()
        self._msg_received.connect(self._handle_message)

        self._timer = QTimer(self)
        self._timer.timeout.connect(self._refresh)
        self._timer.start(1000)

        self._setup_subscription()
        self._refresh()

    # -- UI --------------------------------------------------------------------

    def _build_ui(self):
        layout = QVBoxLayout(self)
        layout.setContentsMargins(4, 4, 4, 4)
        layout.setSpacing(4)

        bar = QHBoxLayout()
        bar.setSpacing(4)
        self._view_button = QPushButton()
        self._view_button.setFlat(True)
        self._view_button.setStyleSheet(f'color: {_DIM};')
        self._view_button.clicked.connect(self._toggle_view)
        self._areas_button = QToolButton()
        self._areas_button.setText('Areas')
        self._areas_button.setStyleSheet(f'color: {_DIM};')
        self._areas_button.setPopupMode(QToolButton.InstantPopup)
        self._areas_menu = QMenu(self._areas_button)
        self._areas_menu.aboutToShow.connect(self._fill_areas_menu)
        self._areas_button.setMenu(self._areas_menu)
        bar.addWidget(self._view_button)
        bar.addWidget(self._areas_button)
        bar.addStretch(1)
        self._notice = QLabel()
        self._notice.setStyleSheet(f'color: {_AMBER};')
        bar.addWidget(self._notice)
        layout.addLayout(bar)

        self._glance_panel = QWidget()
        self._glance_layout = QVBoxLayout(self._glance_panel)
        self._glance_layout.setContentsMargins(0, 0, 0, 0)
        self._glance_layout.setSpacing(4)
        layout.addWidget(self._glance_panel, 1)

        self._detail = QTextBrowser()
        self._detail.setStyleSheet(
            f'background: {_BACKGROUND.name()}; color: {_TEXT}; border: none;')
        layout.addWidget(self._detail, 1)

    def _fill_areas_menu(self):
        self._areas_menu.clear()
        ids = self._store.ids()
        if not ids:
            self._areas_menu.addAction('No areas received yet').setEnabled(False)
            return
        for area_id in ids:
            action = self._areas_menu.addAction(f'Forget area {area_id}')
            action.triggered.connect(
                lambda _checked=False, a=area_id: self.forget_area(a))
        self._areas_menu.addSeparator()
        self._areas_menu.addAction('Clear all areas').triggered.connect(
            self.clear_areas)

    # -- Config ----------------------------------------------------------------

    def get_config(self) -> CoverageConfig:
        return self._config

    def load_config(self, config: CoverageConfig):
        """Apply a configuration; resubscribe only when the topic changed."""
        new = config.normalized()
        topic_changed = new.topic != self._config.topic
        self._config = new
        if topic_changed:
            self._teardown_subscription()
            self._setup_subscription()
        self._tile_keys = None
        self._refresh()

    def set_view(self, view: str):
        self._config = dataclasses.replace(self._config, view=view).normalized()
        self._refresh()

    def _toggle_view(self):
        self.set_view(VIEW_DETAIL if self._config.view == VIEW_GLANCE
                      else VIEW_GLANCE)

    def shutdown(self):
        self._timer.stop()
        self._teardown_subscription()

    # -- Store actions ---------------------------------------------------------

    def forget_area(self, area_id: str):
        self._store.forget(area_id)
        self._refresh()

    def clear_areas(self):
        self._store.clear()
        self._refresh()

    def seen_area_ids(self) -> List[str]:
        return self._store.ids()

    # -- Subscription ----------------------------------------------------------

    def _setup_subscription(self):
        self._generation += 1
        if self._msg_class is None:
            self._node.get_logger().warning(
                'marine_interfaces/CoverageAssessment is not importable; '
                'build marine_interfaces to use the coverage view')
            return
        generation = self._generation
        self._subscription = self._node.create_subscription(
            self._msg_class, self._config.topic,
            lambda msg, g=generation: self._msg_received.emit(g, msg),
            ASSESSMENT_QOS)

    def _teardown_subscription(self):
        # Bumping the generation drops messages already queued for the Qt
        # thread from the subscription being destroyed.
        self._generation += 1
        if self._subscription is not None:
            self._node.destroy_subscription(self._subscription)
            self._subscription = None

    # -- Qt-thread message handling ---------------------------------------------

    def _handle_message(self, generation, msg):
        if generation != self._generation:
            return
        try:
            score = AreaScore.from_msg(msg)
        except Exception as exc:  # a malformed message must not kill the panel
            self._node.get_logger().warning(
                f'Ignoring unreadable CoverageAssessment: {exc!r}')
            return
        if self._store.update(score, self._clock()):
            self._refresh()

    # -- Rendering -------------------------------------------------------------

    def _visible(self):
        """``[(area_id, StoredArea or None)]`` for the selected (else all) areas."""
        cfg = self._config
        ids = cfg.selected_areas or self._store.ids()
        return [(i, self._store.get(i)) for i in ids]

    def _status(self, held: Optional[StoredArea], now):
        """``(text, colour, aging)`` for one area's status line."""
        if held is None:
            return 'waiting for first assessment', _AMBER, True
        state = aging_state(held.received, now, self._config.aging_timeout)
        age = format_age(now - held.received)
        if state == AGING:
            text, color, aging = f'no new assessment for {age}', _AMBER, True
        else:
            text, color, aging = f'updated {age} ago', _DIM, False
        if held.score.clamped:
            text += ' | scorer values inconsistent, clamped'
            color = _AMBER
        return text, color, aging

    def _combined(self, covered, spec):
        if not self._config.show_combined:
            return None
        return format_score(combined_score(
            covered, spec, self._config.weight_spec, self._config.weight_covered))

    def _refresh(self):
        glance = self._config.view == VIEW_GLANCE
        self._glance_panel.setVisible(glance)
        self._detail.setVisible(not glance)
        self._view_button.setText('Detail' if glance else 'Glance')
        entries = self._visible()
        received = [h.score for _, h in entries if h is not None]
        if self._msg_class is None:
            self._notice.setText('marine_interfaces not available')
        elif not entries:
            self._notice.setText(f'waiting for {self._config.topic}')
        else:
            self._notice.setText('')
        if glance:
            self._refresh_glance(entries, received)
        else:
            self._refresh_detail(entries, received)

    def _refresh_glance(self, entries, received):
        now = self._clock()
        rows = []
        if len(received) > 1:
            pooled = pool(received)
            latest = max((h for _, h in entries if h is not None),
                         key=lambda h: h.received)
            text, color, aging = self._status(latest, now)
            rows.append((
                f'All areas (sum of {pooled.count})',
                format_pct(pooled.pct_covered), format_pct(pooled.pct_to_spec),
                self._combined(pooled.pct_covered, pooled.pct_to_spec),
                text, color, aging))
        for area_id, held in entries:
            text, color, aging = self._status(held, now)
            if held is None:
                rows.append((area_id, '--', '--', self._combined(None, None),
                             text, color, aging))
                continue
            s = held.score
            rows.append((s.title, format_pct(s.pct_covered),
                         format_pct(s.pct_to_spec),
                         self._combined(s.pct_covered, s.pct_to_spec),
                         text, color, aging))

        keys = tuple(r[0] for r in rows)
        if keys != self._tile_keys or len(self._tiles) != len(rows):
            self._rebuild_tiles(len(rows))
            self._tile_keys = keys
        for tile, row in zip(self._tiles, rows):
            tile.set_content(*row)
        self._rescale()

    def _rebuild_tiles(self, count):
        for tile in self._tiles:
            self._glance_layout.removeWidget(tile)
            tile.deleteLater()
        self._tiles = []
        for _ in range(count):
            tile = GlanceTile()
            self._glance_layout.addWidget(tile, 1)
            self._tiles.append(tile)

    def _rescale(self):
        """Size the numbers to the space available, times the font scale."""
        if not self._tiles:
            return
        width = max(self._glance_panel.width(), 1)
        height = max(self._glance_panel.height(), 1)
        tile_h = height / len(self._tiles)
        columns = 3 if self._config.show_combined else 2
        big = min(tile_h * 0.45, width / (columns * 5.0))
        scale = self._config.font_scale
        for tile in self._tiles:
            tile.set_pixel_sizes(big * scale, max(big * 0.22, 11) * min(scale, 1.5))

    def resizeEvent(self, event):
        super().resizeEvent(event)
        self._rescale()

    def _refresh_detail(self, entries, received):
        now = self._clock()
        cfg = self._config
        e = html.escape
        parts = [f'<body style="font-size:{int(14 * cfg.font_scale)}px">']
        if len(received) > 1:
            pooled = pool(received)
            line = (f'<h3>All areas (sum of {pooled.count}; overlapping areas '
                    f'count a shared cell twice)</h3>'
                    f'<p>covered {format_pct(pooled.pct_covered)} &nbsp; '
                    f'to standard {format_pct(pooled.pct_to_spec)}')
            comb = self._combined(pooled.pct_covered, pooled.pct_to_spec)
            if comb is not None:
                line += f' &nbsp; combined {comb}'
            parts.append(line + '</p>')
        for area_id, held in entries:
            if held is None:
                parts.append(f'<h3>{e(area_id)}</h3><p>waiting for first assessment</p>')
                continue
            parts.append(self._area_html(held, now))
        parts.append('</body>')
        scroll = self._detail.verticalScrollBar().value()
        self._detail.setHtml(''.join(parts))
        self._detail.verticalScrollBar().setValue(scroll)

    def _area_html(self, held: StoredArea, now) -> str:
        s = held.score
        e = html.escape
        text, _color, aging = self._status(held, now)
        out = [f'<h3>{e(s.title)}</h3>']
        std = (f'{e(s.standard_name)} (a={s.standard_a:g} m, b={s.standard_b:g})'
               if s.standard_name else f'a={s.standard_a:g} m, b={s.standard_b:g}')
        out.append(f'<p>standard: {std}<br>')
        out.append(f'covered {format_pct(s.pct_covered)} &nbsp; '
                   f'to standard {format_pct(s.pct_to_spec)}')
        comb = self._combined(s.pct_covered, s.pct_to_spec)
        if comb is not None:
            out.append(f' &nbsp; combined {comb}')
        out.append('<br>')
        cells = s.cells
        if cells is not None:
            out.append(f'cells (total / covered / to standard): '
                       f'{cells[0]} / {cells[1]} / {cells[2]} '
                       f'at {s.cell_size:g} m<br>')
        sec = s.stamp_ns / 1e9
        out.append(f'last assessment (scorer clock): {sec:.1f} s<br>')
        color = _AMBER if aging or s.clamped else _DIM
        out.append(f'<span style="color:{color}">{e(text)}</span></p>')
        if s.sources:
            out.append('<table border="0" cellspacing="6"><tr>'
                       '<th align="left">source</th><th>covered</th>'
                       '<th>to standard</th><th>unique</th><th>covered m&sup2;</th></tr>')
            for src in s.sources:
                out.append(
                    f'<tr><td>{e(src.source)}</td>'
                    f'<td align="right">{format_pct(fraction(src.covered, s.total))}</td>'
                    f'<td align="right">{format_pct(fraction(src.meeting, s.total))}</td>'
                    f'<td align="right">{format_pct(fraction(src.unique, s.total))}</td>'
                    f'<td align="right">{src.covered:.0f}</td></tr>')
            out.append('</table>')
        return ''.join(out)
