"""Offscreen Qt smoke test: the widget against a stub node, no ROS graph.

Messages are fed through the same slot the subscription callback reaches, so
this covers the Qt-thread path, painting, staleness, errors and the dialog.
"""

import os

import pytest

os.environ.setdefault('QT_QPA_PLATFORM', 'offscreen')

from fake_msgs import detections, flat_bottom  # noqa: E402
from python_qt_binding.QtWidgets import QApplication  # noqa: E402
from rclpy.qos import DurabilityPolicy, HistoryPolicy, ReliabilityPolicy  # noqa: E402

import rqt_ping_profile.ping_profile_widget as pw  # noqa: E402
from rqt_ping_profile.config_dialog import ConfigDialog  # noqa: E402
from rqt_ping_profile.profile_model import ProfileConfig  # noqa: E402


class DummyMsg:
    """Stands in for the SonarDetections class passed to create_subscription."""


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
    monkeypatch.setattr(pw, 'load_message_class', lambda: DummyMsg)
    node, clock = StubNode(), Clock()
    widget = pw.PingProfileWidget(node, clock=clock)
    widget.resize(600, 420)
    yield widget, node, clock
    widget.shutdown()


def feed(widget, node, msg):
    """Deliver ``msg`` the way the subscription callback does."""
    callback = node.subscriptions[-1][2]
    callback(msg)
    QApplication.processEvents()


def render(widget):
    image = widget.grab().toImage()
    assert not image.isNull()
    return image


def distinct_colours(image):
    return len({image.pixel(x, y) for x in range(0, image.width(), 3)
                for y in range(0, image.height(), 3)})


def test_subscription_defaults(env):
    _widget, node, _clock = env
    assert len(node.subscriptions) == 1
    msg_class, topic, _cb, qos = node.subscriptions[0]
    assert msg_class is DummyMsg
    assert topic == 'sensors/mbes/detections'
    assert qos.reliability == ReliabilityPolicy.BEST_EFFORT
    assert qos.durability == DurabilityPolicy.VOLATILE
    assert qos.history == HistoryPolicy.KEEP_LAST and qos.depth == 1


def test_waiting_before_any_ping_renders(env):
    widget, _node, _clock = env
    text, problem = widget.status_text()
    assert 'waiting for the first ping' in text and problem
    render(widget)


def test_ping_is_drawn_and_profile_points_are_the_ok_beams(env):
    widget, node, _clock = env
    blank = distinct_colours(render(widget))
    feed(widget, node, flat_bottom(depth=20.0, beams=9))
    assert len(widget.latest_profile()) == 9
    assert all(abs(d - 20.0) < 1e-3 for _a, d in widget.latest_profile())
    assert widget.status_text() == ('', False)
    assert distinct_colours(render(widget)) > blank


def test_stale_indicator_after_timeout_and_cleared_by_a_ping(env):
    widget, node, clock = env
    feed(widget, node, flat_bottom())
    clock.now += 4.0
    assert widget.status_text()[0] == ''
    clock.now += 2.0
    text, problem = widget.status_text()
    assert text.startswith('NO PING FOR 6 s') and problem
    render(widget)
    feed(widget, node, flat_bottom())
    assert widget.status_text() == ('', False)


def test_stale_timeout_zero_never_marks_stale(env):
    widget, node, clock = env
    widget.load_config(ProfileConfig(stale_timeout=0.0))
    feed(widget, node, flat_bottom())
    clock.now += 1e5
    assert widget.status_text()[0] == ''


@pytest.mark.parametrize('bad', [
    detections([], []),
    detections([0.03, 0.03], [0.0]),
    detections([0.03], [0.0], sound_speed=0.0),
    object(),
])
def test_bad_message_draws_nothing_and_does_not_raise(env, bad):
    widget, node, _clock = env
    feed(widget, node, bad)
    assert widget.latest_profile() == []
    text, problem = widget.status_text()
    assert 'unusable ping' in text and problem
    render(widget)


def test_bad_message_is_logged_once_per_reason_not_per_ping(env):
    widget, node, clock = env
    for _ in range(20):
        feed(widget, node, detections([], []))
        clock.now += 0.1
    assert len(node.logger.warnings) == 1
    clock.now += 11.0
    feed(widget, node, detections([], []))
    assert len(node.logger.warnings) == 2
    feed(widget, node, detections([0.03, 0.03], [0.0]))      # a new reason at once
    assert len(node.logger.warnings) == 3


def test_bad_message_after_a_good_one_keeps_the_picture_and_says_so(env):
    widget, node, _clock = env
    feed(widget, node, flat_bottom())
    feed(widget, node, detections([], []))
    assert len(widget.latest_profile()) == 11
    assert 'unusable ping ignored' in widget.status_text()[0]


def test_all_flagged_ping_says_no_ok_detections(env):
    widget, node, _clock = env
    feed(widget, node, flat_bottom(flags=[1] * 11))
    assert 'no OK detections' in widget.status_text()[0]
    render(widget)


def test_auto_mode_and_history_and_flagged_render(env):
    widget, node, _clock = env
    widget.load_config(ProfileConfig(depth_mode='auto', history=5, show_flagged=True,
                                     title='Boat 1'))
    for depth in (10.0, 40.0, 35.0):
        feed(widget, node, flat_bottom(depth=depth, flags=[4] + [0] * 10))
    top, bottom = widget._scale.depth_range
    assert top == 0.0 and bottom == pytest.approx(40.0 * 1.10, rel=1e-3)
    assert len(widget._store.history()) == 2
    render(widget)


def test_tiny_and_huge_widget_sizes_render(env):
    widget, node, _clock = env
    feed(widget, node, flat_bottom())
    for w, h in ((10, 10), (4000, 2160)):
        widget.resize(w, h)
        render(widget)


def test_fixed_range_with_deeper_water_renders_clipped(env):
    widget, node, _clock = env
    feed(widget, node, flat_bottom(depth=500.0))
    render(widget)
    assert widget._paint_error == ''


def test_topic_change_resubscribes_and_clears(env):
    widget, node, _clock = env
    feed(widget, node, flat_bottom())
    old = node.subscriptions[-1]
    widget.load_config(ProfileConfig(topic='/boat2/sensors/mbes/detections'))
    assert node.destroyed == [old]
    assert node.subscriptions[-1][1] == '/boat2/sensors/mbes/detections'
    assert widget.latest_profile() == []
    old[2](flat_bottom())                    # late message from the old subscription
    QApplication.processEvents()
    assert widget.latest_profile() == []


def test_failed_subscribe_is_shown_and_retried(env):
    widget, node, _clock = env
    node.fail_topics.add('bad topic')
    widget.load_config(ProfileConfig(topic='bad topic'))
    assert 'cannot subscribe' in widget.status_text()[0]
    assert node.logger.errors
    widget.load_config(ProfileConfig(topic='good'))
    assert node.subscriptions[-1][1] == 'good'
    assert 'waiting' in widget.status_text()[0]


def test_config_round_trips_through_the_dialog(qapp):
    cfg = ProfileConfig(topic='/b/x', title='B', depth_mode='auto', depth_min=1.0,
                        depth_max=60.0, stale_timeout=9.0, history=4, show_flagged=True)
    assert ConfigDialog(cfg).get_config() == cfg
    assert ConfigDialog(ProfileConfig()).get_config() == ProfileConfig()
