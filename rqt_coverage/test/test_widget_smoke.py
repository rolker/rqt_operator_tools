"""Offscreen Qt smoke test: the widget against a stub node, no ROS graph.

Messages are fed through the same slot the subscription callback reaches, so
this covers the Qt-thread path, both views, aging and the subscription QoS.
"""

import os

import pytest

os.environ.setdefault('QT_QPA_PLATFORM', 'offscreen')

from fake_msgs import assessment, source  # noqa: E402
from python_qt_binding.QtCore import Qt  # noqa: E402
from python_qt_binding.QtWidgets import QApplication  # noqa: E402
from rclpy.qos import DurabilityPolicy, HistoryPolicy, ReliabilityPolicy  # noqa: E402

import rqt_coverage.coverage_widget as cw  # noqa: E402
from rqt_coverage.score_model import CoverageConfig  # noqa: E402


class DummyMsg:
    """Stands in for the CoverageAssessment class passed to create_subscription."""


class StubLogger:
    def __init__(self):
        self.warnings = []
        self.errors = []

    def warning(self, text):
        self.warnings.append(text)

    def error(self, text):
        self.errors.append(text)


class StubNode:
    def __init__(self):
        self.fail_topics = set()
        self.subscriptions = []
        self.destroyed = []
        self.logger = StubLogger()

    def create_subscription(self, msg_class, topic, callback, qos):
        if topic in self.fail_topics:
            raise ValueError(f'invalid topic name: {topic}')
        sub = (msg_class, topic, callback, qos)
        self.subscriptions.append(sub)
        return sub

    def destroy_subscription(self, sub):
        self.destroyed.append(sub)

    def get_logger(self):
        return self.logger


class Clock:
    def __init__(self):
        self.now = 1000.0

    def __call__(self):
        return self.now


@pytest.fixture(scope='module')
def qapp():
    return QApplication.instance() or QApplication([])


@pytest.fixture
def env(qapp, monkeypatch):
    monkeypatch.setattr(cw, 'load_assessment_class', lambda: DummyMsg)
    node, clock = StubNode(), Clock()
    widget = cw.CoverageWidget(node, clock=clock)
    widget.resize(900, 400)
    yield widget, node, clock
    widget.shutdown()


def feed(widget, node, msg):
    """Deliver ``msg`` the way the subscription callback does."""
    _cls, _topic, callback, _qos = node.subscriptions[-1]
    callback(msg)
    QApplication.processEvents()   # the coalesced redraw runs from the event loop


def texts(widget):
    out = []
    for t in widget._tiles:
        out.append((t.title_label.text(), t.covered_value.text(), t.spec_value.text(),
                    t.status_label.text()))
    return out


def test_subscription_qos_is_reliable_transient_local_depth_100(env):
    _widget, node, _clock = env
    assert len(node.subscriptions) == 1
    msg_class, topic, _cb, qos = node.subscriptions[0]
    assert msg_class is DummyMsg
    assert topic == 'coverage_assessment'
    assert qos.reliability == ReliabilityPolicy.RELIABLE
    assert qos.durability == DurabilityPolicy.TRANSIENT_LOCAL
    assert qos.history == HistoryPolicy.KEEP_LAST
    assert qos.depth == 100


def test_waiting_before_any_message(env):
    widget, _node, _clock = env
    assert 'waiting for coverage_assessment' in widget._notice.text()
    assert widget._tiles == []


def test_glance_single_area(env):
    widget, node, _clock = env
    feed(widget, node, assessment('lake', total=1000, covered=600, meeting=300))
    assert texts(widget) == [('lake', '60.0 %', '30.0 %', 'received 0 s ago')]
    assert widget._config.view == 'glance'


def test_glance_three_areas_adds_pooled_tile_first(env):
    widget, node, _clock = env
    feed(widget, node, assessment('a', total=9000, covered=9000, meeting=9000))
    feed(widget, node, assessment('b', total=1000, covered=0, meeting=0))
    feed(widget, node, assessment('c', total=1000, covered=500, meeting=0))
    rows = texts(widget)
    assert [r[0] for r in rows] == ['All areas (sum of 3)', 'a', 'b', 'c']
    assert rows[0][1] == '86.4 %'    # (9000 + 0 + 500) / 11000


def test_selected_area_filter_and_placeholder(env):
    widget, node, _clock = env
    feed(widget, node, assessment('a'))
    feed(widget, node, assessment('b'))
    widget.load_config(CoverageConfig(selected_areas=['b', 'later']))
    rows = texts(widget)
    assert [r[0] for r in rows] == ['b', 'later']
    assert rows[1][1:] == ('--', '--', 'waiting for first assessment')


def test_detail_view_shows_standard_sources_and_cells(env):
    widget, node, _clock = env
    feed(widget, node, assessment(
        'lake', total=1000, covered=600, meeting=300, cell_size=2.0,
        std_name='S-44 <1a>', sources=[source('boat<1>', 400, 200, 100),
                                       source('boat2', 300, 150, 0)]))
    widget.set_view('detail')
    text = widget._detail.toPlainText()
    assert 'lake' in text
    assert 'S-44 <1a>' in text            # escaped, shown literally
    assert 'boat<1>' in text and 'boat2' in text
    assert '250 / 150 / 75' in text       # cells at 2 m
    assert 'covered 60.0 %' in text
    assert widget.get_config().view == 'detail'


def test_combined_score_shown_with_weights(env):
    widget, node, _clock = env
    widget.load_config(CoverageConfig(show_combined=True, weight_covered=0.5))
    feed(widget, node, assessment('a', total=1000, covered=600, meeting=300))
    tile = widget._tiles[0]
    assert tile.combined_value.text() == '45.0 %'
    assert not tile._combined_col.isHidden()


def test_combined_hidden_by_default(env):
    widget, node, _clock = env
    feed(widget, node, assessment('a'))
    assert widget._tiles[0]._combined_col.isHidden()


def test_aging_indicator_after_the_timeout(env):
    widget, node, clock = env
    feed(widget, node, assessment('a'))
    clock.now += 75
    widget._refresh()
    assert widget._tiles[0].status_label.text() == 'no new assessment for 75 s'
    clock.now += 0
    feed(widget, node, assessment('a', sec=200))
    assert widget._tiles[0].status_label.text() == 'received 0 s ago'


def test_forget_and_clear(env):
    widget, node, _clock = env
    feed(widget, node, assessment('a'))
    feed(widget, node, assessment('b'))
    widget.forget_area('a')
    assert [r[0] for r in texts(widget)] == ['b']
    widget.clear_areas()
    assert widget._tiles == []


def test_topic_change_resubscribes_and_drops_queued_messages(env):
    widget, node, _clock = env
    stale_callback = node.subscriptions[0][2]
    widget.load_config(CoverageConfig(topic='/other/coverage'))
    assert len(node.subscriptions) == 2
    assert node.destroyed == [node.subscriptions[0]]
    assert node.subscriptions[1][1] == '/other/coverage'
    stale_callback(assessment('ghost'))     # queued before teardown
    assert widget._tiles == []


def test_same_topic_does_not_resubscribe(env):
    widget, node, _clock = env
    widget.load_config(CoverageConfig(view='detail', font_scale=2.0))
    assert len(node.subscriptions) == 1


def test_malformed_message_is_ignored_not_fatal(env):
    widget, node, _clock = env
    feed(widget, node, object())
    assert widget._tiles == []
    assert node.logger.warnings
    feed(widget, node, assessment('a'))     # still alive
    assert len(widget._tiles) == 1


def test_inconsistent_scorer_values_are_flagged(env):
    widget, node, _clock = env
    feed(widget, node, assessment('a', total=100, covered=40, meeting=70))
    assert 'clamped' in widget._tiles[0].status_label.text()


def test_zero_total_area_shows_dashes(env):
    widget, node, _clock = env
    feed(widget, node, assessment('tiny', total=0, covered=0, meeting=0))
    assert texts(widget)[0][1:3] == ('--', '--')


def test_font_scale_changes_the_number_size(env):
    widget, node, _clock = env
    feed(widget, node, assessment('a'))
    widget.show()
    widget.load_config(CoverageConfig(font_scale=1.0))
    small = widget._tiles[0].covered_value.font().pixelSize()
    widget.load_config(CoverageConfig(font_scale=2.0))
    big = widget._tiles[0].covered_value.font().pixelSize()
    assert big > small


def test_topic_change_clears_areas_of_the_old_topic(env):
    widget, node, _clock = env
    feed(widget, node, assessment('a'))
    assert len(widget._tiles) == 1
    widget.load_config(CoverageConfig(topic='/other/coverage'))
    assert widget._tiles == []
    feed(widget, node, assessment('a', sec=1))      # same id, earlier stamp: accepted
    assert len(widget._tiles) == 1


def test_bad_topic_is_reported_and_retried(env):
    widget, node, _clock = env
    node.fail_topics.add('bad topic')
    widget.load_config(CoverageConfig(topic='bad topic'))
    assert 'cannot subscribe to bad topic' in widget._notice.text()
    assert node.logger.errors
    node.fail_topics.clear()
    widget.load_config(CoverageConfig(topic='bad topic'))   # apply again: retried
    assert widget._notice.text() == 'waiting for bad topic'
    assert node.subscriptions[-1][1] == 'bad topic'


def test_pooled_status_follows_the_oldest_area(env):
    widget, node, clock = env
    feed(widget, node, assessment('stale'))
    clock.now += 90
    feed(widget, node, assessment('live'))
    widget._refresh()
    pooled, live, stale = widget._tiles
    assert pooled.status_label.text() == 'no new assessment for 90 s'
    assert stale.status_label.text() == 'no new assessment for 90 s'
    assert live.status_label.text() == 'received 0 s ago'
    widget.set_view('detail')
    assert 'no new assessment for 90 s' in widget._detail.toPlainText()


def test_pooled_title_counts_waiting_areas(env):
    widget, node, _clock = env
    feed(widget, node, assessment('a'))
    feed(widget, node, assessment('b'))
    widget.load_config(CoverageConfig(selected_areas=['a', 'b', 'c']))
    assert widget._tiles[0].title_label.text() == 'All areas (sum of 2, 1 waiting)'


def test_markup_in_area_ids_is_shown_literally(env):
    widget, node, _clock = env
    feed(widget, node, assessment('<b>north&south'))
    assert widget._tiles[0].title_label.text() == '<b>north&south'
    assert widget._tiles[0].title_label.textFormat() == Qt.PlainText


def test_rendering_error_is_contained(env, monkeypatch):
    widget, node, _clock = env
    feed(widget, node, assessment('a'))
    monkeypatch.setattr(cw, 'format_pct',
                        lambda *_a: (_ for _ in ()).throw(RuntimeError('boom')))
    widget._refresh()                      # must not raise
    assert 'display error' in widget._notice.text()
    assert node.logger.errors


def test_burst_of_messages_redraws_once(env):
    widget, node, _clock = env
    calls = []
    original = widget._refresh_unguarded
    widget._refresh_unguarded = lambda: (calls.append(1), original())[1]
    _cls, _topic, callback, _qos = node.subscriptions[-1]
    for i in range(100):
        callback(assessment(f'a{i % 5}', sec=i))
    QApplication.processEvents()
    assert len(calls) == 1
    assert len(widget._tiles) == 6         # pooled + 5 areas


def test_glance_columns_share_the_width_without_combined(env):
    widget, node, _clock = env
    feed(widget, node, assessment('a'))
    widget.show()
    QApplication.processEvents()
    tile = widget._tiles[0]
    used = tile.covered_value.width() + tile.spec_value.width()
    assert used > 0.85 * tile.width() - 40   # no empty third column reserved


def test_detail_scroll_position_survives_the_refresh_timer(env):
    widget, node, clock = env
    for i in range(30):
        feed(widget, node, assessment(f'area{i:02d}', sources=[source('b1', 1, 1, 1)]))
    widget.set_view('detail')
    widget.show()
    QApplication.processEvents()
    bar = widget._detail.verticalScrollBar()
    assert bar.maximum() > 100
    bar.setValue(bar.maximum() // 2)
    wanted = bar.value()
    clock.now += 3          # the age text changes, so the HTML changes
    widget._refresh()
    assert abs(bar.value() - wanted) <= 2
