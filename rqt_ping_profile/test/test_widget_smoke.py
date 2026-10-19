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


class DummyPlatforms:
    """Stands in for the PlatformList class passed to create_subscription."""


class Sub(tuple):
    """A subscription handle: the 4-tuple, plus the resolved ``topic_name``."""


class StubNode:
    def __init__(self):
        self.remap = {}
        self.fail_topics = set()
        self.subscriptions = []         # detections subscriptions only
        self.platform_subs = []
        self.destroyed = []
        self.logger = StubLogger()

    def create_subscription(self, msg_class, topic, callback, qos):
        if topic in self.fail_topics:
            raise ValueError(f'invalid topic name: {topic}')
        sub = Sub((msg_class, topic, callback, qos))
        sub.topic_name = self.remap.get(topic, topic)
        (self.platform_subs if msg_class is DummyPlatforms else self.subscriptions).append(sub)
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
    monkeypatch.setattr(pw, 'load_platforms_class', lambda: DummyPlatforms)
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
    text, problem = widget._ping_status()
    assert 'waiting for the first ping' in text and problem
    render(widget)


def test_ping_is_drawn_and_profile_points_are_the_ok_beams(env):
    widget, node, _clock = env
    blank = distinct_colours(render(widget))
    feed(widget, node, flat_bottom(depth=20.0, beams=9))
    assert len(widget.latest_profile()) == 9
    assert all(abs(d - 20.0) < 1e-3 for _a, d in widget.latest_profile())
    assert widget._ping_status() == ('', False)
    assert distinct_colours(render(widget)) > blank


def test_stale_indicator_after_timeout_and_cleared_by_a_ping(env):
    widget, node, clock = env
    feed(widget, node, flat_bottom())
    clock.now += 4.0
    assert widget._ping_status()[0] == ''
    clock.now += 2.0
    text, problem = widget._ping_status()
    assert text.startswith('NO PING FOR 6 s') and problem
    render(widget)
    feed(widget, node, flat_bottom())
    assert widget._ping_status() == ('', False)


def test_stale_timeout_zero_never_marks_stale(env):
    widget, node, clock = env
    widget.load_config(ProfileConfig(stale_timeout=0.0))
    feed(widget, node, flat_bottom())
    clock.now += 1e5
    assert widget._ping_status()[0] == ''


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
    text, problem = widget._ping_status()
    assert 'unusable ping' in text and problem
    render(widget)


def test_bad_messages_are_logged_at_most_once_per_interval(env):
    widget, node, clock = env
    for i in range(40):
        # alternating faults and good pings must not get around the limit
        feed(widget, node, detections([], []) if i % 2 else
             detections([0.03, 0.03], [0.0] * (1 + i % 3)))
        feed(widget, node, flat_bottom()) if i % 4 == 0 else None
        clock.now += 0.1
    assert len(node.logger.warnings) == 1
    clock.now += 11.0
    feed(widget, node, detections([], []))
    assert len(node.logger.warnings) == 2


def test_only_the_newest_message_is_handled_and_keeps_its_receipt_time(env):
    widget, node, clock = env
    callback = node.subscriptions[-1][2]
    widget.blockSignals(True)               # the GUI thread is "busy"
    callback(flat_bottom(depth=10.0))
    clock.now += 3.0
    callback(flat_bottom(depth=25.0))
    clock.now += 4.0
    widget.blockSignals(False)
    widget._take_message()
    assert widget._store.count == 1         # one slot: the older ping was replaced
    assert widget.latest_profile()[0][1] == pytest.approx(25.0, abs=1e-3)
    assert widget._store.received == 1003.0  # when it arrived, not when handled


def test_all_nan_ping_does_not_erase_the_last_good_one(env):
    widget, node, clock = env
    feed(widget, node, flat_bottom(depth=20.0))
    clock.now += 4.0
    nan = float('nan')
    feed(widget, node, detections([nan] * 3, [0.0, 0.1, 0.2]))
    assert len(widget.latest_profile()) == 11
    assert widget._store.received == 1000.0        # not refreshed
    assert 'no usable beams' in widget._ping_status()[0]


def test_long_title_does_not_push_the_status_off_screen(env):
    widget, node, clock = env
    widget.load_config(ProfileConfig(title='x' * 300))
    widget.resize(200, 150)
    feed(widget, node, flat_bottom())
    clock.now += 30.0
    assert widget._ping_status()[1]
    render(widget)


def test_topic_change_resets_the_auto_scale(env):
    widget, node, _clock = env
    widget.load_config(ProfileConfig(depth_mode='auto'))
    feed(widget, node, flat_bottom(depth=200.0))
    assert widget._scale.depth_range[1] > 200
    widget.load_config(ProfileConfig(depth_mode='auto', topic='/b/sensors/mbes/detections'))
    feed(widget, node, flat_bottom(depth=20.0))
    assert widget._scale.depth_range[1] == pytest.approx(22.0, rel=1e-3)


def test_stale_dims_history_and_renders(env):
    widget, node, clock = env
    widget.load_config(ProfileConfig(history=3))
    for _ in range(4):
        feed(widget, node, flat_bottom())
    clock.now += 10.0
    render(widget)
    assert widget._paint_error == ''


def test_bad_message_after_a_good_one_keeps_the_picture_and_says_so(env):
    widget, node, _clock = env
    feed(widget, node, flat_bottom())
    feed(widget, node, detections([], []))
    assert len(widget.latest_profile()) == 11
    assert 'unusable ping ignored' in widget._ping_status()[0]


def test_all_flagged_ping_says_no_ok_detections(env):
    widget, node, _clock = env
    feed(widget, node, flat_bottom(flags=[1] * 11))
    assert 'no OK detections' in widget._ping_status()[0]
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
    assert 'cannot subscribe' in widget._ping_status()[0]
    assert node.logger.errors
    widget.load_config(ProfileConfig(topic='good'))
    assert node.subscriptions[-1][1] == 'good'
    assert 'waiting' in widget._ping_status()[0]


def test_config_round_trips_through_the_dialog(qapp):
    cfg = ProfileConfig(topic='/b/x', title='B', depth_mode='auto', depth_min=1.0,
                        depth_max=60.0, stale_timeout=9.0, history=4, show_flagged=True)
    assert ConfigDialog(cfg).get_config() == cfg
    assert ConfigDialog(ProfileConfig()).get_config() == ProfileConfig()


# -- Platform colour (#115) ------------------------------------------------------

from types import SimpleNamespace as NS  # noqa: E402

from python_qt_binding.QtGui import QColor  # noqa: E402

URCHIN = '/urchin/sensors/mbes/detections'
MAGENTA = (1.0, 0.0, 0.5, 1.0)          # nothing in the default palette
DEFAULTS = (pw._SWATH, pw._PROFILE)


def plist(*boats):
    """boats: (name, namespace, (r, g, b, a))"""
    return NS(platforms=[NS(name=n, platform_namespace=ns,
                            color=NS(r=c[0], g=c[1], b=c[2], a=c[3]))
                         for n, ns, c in boats])


def feed_platforms(node, msg):
    node.platform_subs[-1][2](msg)
    QApplication.processEvents()


def count_pixels(image, qcolor):
    target = QColor(qcolor).rgb()
    return sum(1 for x in range(image.width()) for y in range(image.height())
               if image.pixel(x, y) == target)


def drawn_with(widget, node, colour):
    """Feed a ping, render, and count pixels of exactly ``colour``."""
    feed(widget, node, flat_bottom(depth=15.0))
    return count_pixels(render(widget), colour)


def test_platform_subscription_matches_the_publisher_qos(env):
    _widget, node, _clock = env
    assert len(node.platform_subs) == 1
    msg_class, topic, _cb, qos = node.platform_subs[0]
    assert msg_class is DummyPlatforms and topic == '/marine/platforms'
    assert qos.reliability == ReliabilityPolicy.RELIABLE
    assert qos.durability == DurabilityPolicy.VOLATILE
    assert qos.history == HistoryPolicy.KEEP_LAST and qos.depth == 5


def test_no_match_keeps_default_colours_and_the_topic_title(env):
    widget, node, _clock = env
    feed_platforms(node, plist(('Bluefin', 'bluefin', MAGENTA)))
    assert widget.effective_color() is None
    assert widget.display_title() == 'sensors/mbes/detections'
    assert widget.status_text()[0].endswith('colour: default (no platform match)')
    feed(widget, node, flat_bottom())
    image = render(widget)
    assert count_pixels(image, pw._PROFILE) > 0 and count_pixels(image, pw._SWATH) > 0


def test_match_draws_swath_and_profile_in_the_platform_colour(env):
    widget, node, _clock = env
    widget.load_config(ProfileConfig(topic=URCHIN))
    feed_platforms(node, plist(('Urchin', 'urchin', MAGENTA)))
    assert widget.effective_color().rgb() == QColor.fromRgbF(*MAGENTA[:3]).rgb()
    assert drawn_with(widget, node, widget.effective_color()) > 50
    image = render(widget)
    assert count_pixels(image, pw._PROFILE) == 0 and count_pixels(image, pw._SWATH) == 0
    assert widget.status_text() == ('colour: platform Urchin', False)


def test_title_defaults_to_the_platform_name_and_the_setting_wins(env):
    widget, node, _clock = env
    widget.load_config(ProfileConfig(topic=URCHIN))
    assert widget.display_title() == URCHIN
    feed_platforms(node, plist(('Urchin', 'urchin', MAGENTA)))
    assert widget.display_title() == 'Urchin'
    widget.load_config(ProfileConfig(topic=URCHIN, title='Boat 1'))
    assert widget.display_title() == 'Boat 1'


def test_title_is_drawn_in_the_accent(env):
    widget, node, _clock = env
    widget.load_config(ProfileConfig(topic=URCHIN))
    feed_platforms(node, plist(('Urchin', 'urchin', MAGENTA)))
    top = render(widget).copy(0, 0, 300, 24)
    assert count_pixels(top, widget.effective_color()) > 0
    assert count_pixels(top, pw._TEXT) == 0


def test_override_wins_over_the_platform_colour(env):
    widget, node, _clock = env
    widget.load_config(ProfileConfig(topic=URCHIN, color='#00ff00'))
    feed_platforms(node, plist(('Urchin', 'urchin', MAGENTA)))
    assert widget.effective_color().rgb() == QColor('#00ff00').rgb()
    assert drawn_with(widget, node, '#00ff00') > 50
    assert count_pixels(render(widget), QColor.fromRgbF(*MAGENTA[:3])) == 0
    assert widget.status_text()[0] == 'colour: manual override'


def test_override_colours_a_sonar_that_is_not_a_platform(env):
    widget, node, _clock = env
    widget.load_config(ProfileConfig(color='#00ff00'))      # default, namespace-less topic
    assert drawn_with(widget, node, '#00ff00') > 50
    assert widget.display_title() == 'sensors/mbes/detections'


def test_clearing_the_override_returns_to_the_platform_colour(env):
    widget, node, _clock = env
    widget.load_config(ProfileConfig(topic=URCHIN, color='#00ff00'))
    feed_platforms(node, plist(('Urchin', 'urchin', MAGENTA)))
    widget.load_config(ProfileConfig(topic=URCHIN))
    assert widget.effective_color().rgb() == QColor.fromRgbF(*MAGENTA[:3]).rgb()


def test_alpha_zero_platform_has_no_colour_but_gives_its_name(env):
    widget, node, _clock = env
    widget.load_config(ProfileConfig(topic=URCHIN))
    feed_platforms(node, plist(('Urchin', 'urchin', (1.0, 0.0, 0.0, 0.0))))
    assert widget.effective_color() is None
    assert widget.display_title() == 'Urchin'
    assert 'has no colour' in widget.status_text()[0]
    feed(widget, node, flat_bottom())
    assert count_pixels(render(widget), pw._PROFILE) > 0


def test_live_update_when_the_list_changes(env):
    widget, node, _clock = env
    widget.load_config(ProfileConfig(topic=URCHIN))
    feed_platforms(node, plist(('Urchin', 'urchin', MAGENTA)))
    feed(widget, node, flat_bottom(depth=15.0))
    assert count_pixels(render(widget), QColor.fromRgbF(*MAGENTA[:3])) > 50
    feed_platforms(node, plist(('Urchin', 'urchin', (0.0, 1.0, 1.0, 1.0))))
    image = render(widget)
    assert count_pixels(image, QColor.fromRgbF(0.0, 1.0, 1.0)) > 50
    assert count_pixels(image, QColor.fromRgbF(*MAGENTA[:3])) == 0


def test_lists_of_other_boats_do_not_make_this_boat_forget_its_colour(env):
    widget, node, _clock = env
    widget.load_config(ProfileConfig(topic=URCHIN))
    feed_platforms(node, plist(('Urchin', 'urchin', MAGENTA)))
    feed_platforms(node, plist(('Bluefin', 'bluefin', (0.0, 0.0, 1.0, 1.0))))
    assert widget.effective_color().rgb() == QColor.fromRgbF(*MAGENTA[:3]).rgb()


def test_topic_change_re_resolves(env):
    widget, node, _clock = env
    feed_platforms(node, plist(('Urchin', 'urchin', MAGENTA),
                               ('Bluefin', 'bluefin', (0.0, 0.0, 1.0, 1.0))))
    assert widget.effective_color() is None
    widget.load_config(ProfileConfig(topic=URCHIN))
    assert widget.display_title() == 'Urchin'
    widget.load_config(ProfileConfig(topic='/bluefin/sensors/mbes/detections'))
    assert widget.display_title() == 'Bluefin'
    assert widget.effective_color().rgb() == QColor(0, 0, 255).rgb()


def test_platform_hand_off_keeps_the_newest_per_boat_and_drops_no_boat(env):
    widget, node, _clock = env
    widget.load_config(ProfileConfig(topic=URCHIN))
    callback = node.platform_subs[-1][2]
    widget.blockSignals(True)               # the GUI thread is "busy"
    callback(plist(('Urchin', 'urchin', (1.0, 0.0, 0.0, 1.0))))
    callback(plist(('Bluefin', 'bluefin', (0.0, 0.0, 1.0, 1.0))))
    callback(plist(('Urchin', 'urchin', (0.0, 1.0, 0.0, 1.0))))
    widget.blockSignals(False)
    widget._take_platforms()
    assert widget.effective_color().rgb() == QColor(0, 255, 0).rgb()
    widget.load_config(ProfileConfig(topic='/bluefin/sensors/mbes/detections'))
    assert widget.effective_color().rgb() == QColor(0, 0, 255).rgb()


def test_garbage_platform_list_is_ignored(env):
    widget, node, _clock = env
    feed_platforms(node, object())
    feed_platforms(node, NS(platforms=[object(), None]))
    assert widget.effective_color() is None
    render(widget)
    assert widget._paint_error == ''


def test_missing_marine_interfaces_degrades_to_defaults(qapp, monkeypatch):
    monkeypatch.setattr(pw, 'load_message_class', lambda: DummyMsg)
    monkeypatch.setattr(pw, 'load_platforms_class', lambda: None)
    node = StubNode()
    widget = pw.PingProfileWidget(node, config=ProfileConfig(topic=URCHIN))
    try:
        assert node.platform_subs == [] and node.logger.warnings
        assert 'marine_interfaces is not importable' in widget.status_text()[0]
        render(widget)
    finally:
        widget.shutdown()


def test_failed_platform_subscription_is_reported_not_fatal(qapp, monkeypatch):
    monkeypatch.setattr(pw, 'load_message_class', lambda: DummyMsg)
    monkeypatch.setattr(pw, 'load_platforms_class', lambda: DummyPlatforms)
    node = StubNode()
    node.fail_topics.add('/marine/platforms')
    widget = pw.PingProfileWidget(node)
    try:
        assert 'cannot subscribe to /marine/platforms' in widget.status_text()[0]
        assert node.logger.errors
    finally:
        widget.shutdown()


def test_shutdown_destroys_the_platform_subscription(env):
    widget, node, _clock = env
    sub = node.platform_subs[-1]
    widget.shutdown()
    assert sub in node.destroyed


def test_dialog_round_trips_the_colour_and_refuses_a_bad_one(qapp):
    cfg = ProfileConfig(color='#ff8800')
    assert ConfigDialog(cfg).get_config().color == '#ff8800'
    dialog = ConfigDialog(ProfileConfig())
    dialog._color.setText('orange')
    dialog.accept()
    assert dialog.result() == 0 and 'orange' in dialog._error.text()
    dialog._color.setText('#FF8800')
    dialog.accept()
    assert dialog.result() == 1 and dialog.get_config().color == '#ff8800'


def test_identity_follows_the_resolved_remapped_topic(env):
    widget, node, _clock = env
    node.remap[URCHIN] = '/bluefin/sensors/mbes/detections'
    widget.load_config(ProfileConfig(topic=URCHIN))      # resubscribes: remapped
    feed_platforms(node, plist(('Urchin', 'urchin', MAGENTA),
                               ('Bluefin', 'bluefin', (0.0, 0.0, 1.0, 1.0))))
    assert widget.resolved_topic() == '/bluefin/sensors/mbes/detections'
    assert widget.display_title() == 'Bluefin'
    assert widget.effective_color().rgb() == QColor(0, 0, 255).rgb()
