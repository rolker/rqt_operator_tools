"""Real rclpy: a published SonarDetections reaches the widget through the executor thread.

Covers what the stub-node tests cannot: the best_effort QoS actually matching
a publisher, and the cross-thread hand-off from an executor thread to the Qt
thread.  Runs on a private DDS domain, offscreen.
"""

import math
import os
import random
import threading
import time

import pytest

os.environ.setdefault('QT_QPA_PLATFORM', 'offscreen')

rclpy = pytest.importorskip('rclpy')
msgs = pytest.importorskip('marine_acoustic_msgs.msg')

from python_qt_binding.QtWidgets import QApplication  # noqa: E402
from rclpy.executors import SingleThreadedExecutor  # noqa: E402
from rclpy.qos import QoSProfile, ReliabilityPolicy  # noqa: E402

from rqt_ping_profile.ping_profile_widget import PingProfileWidget  # noqa: E402
from rqt_ping_profile.profile_model import ProfileConfig  # noqa: E402


def wait_for(predicate, seconds=8.0):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        QApplication.processEvents()
        if predicate():
            return True
        time.sleep(0.02)
    return False


@pytest.fixture
def ros():
    rclpy.init(domain_id=random.randint(100, 200))
    node = rclpy.create_node('rqt_ping_profile_test')
    executor = SingleThreadedExecutor()
    executor.add_node(node)
    thread = threading.Thread(target=executor.spin, daemon=True)
    thread.start()
    yield node
    executor.shutdown()
    thread.join(timeout=2.0)
    node.destroy_node()
    rclpy.shutdown()


def make_msg(depth=20.0, beams=7):
    msg = msgs.SonarDetections()
    msg.ping_info.sound_speed = 1500.0
    for i in range(beams):
        angle = math.radians(-45 + 90 * i / (beams - 1))
        msg.rx_angles.append(angle)
        msg.two_way_travel_times.append(2 * depth / math.cos(angle) / 1500.0)
        msg.flags.append(msgs.DetectionFlag(flag=msgs.DetectionFlag.DETECT_OK))
    return msg


@pytest.mark.parametrize('reliability', [ReliabilityPolicy.RELIABLE,
                                         ReliabilityPolicy.BEST_EFFORT])
def test_published_ping_reaches_the_widget(ros, reliability):
    app = QApplication.instance() or QApplication([])   # noqa: F841
    topic = f'/ping_profile_test_{reliability.name.lower()}'
    widget = PingProfileWidget(ros, config=ProfileConfig(topic=topic))
    pub = ros.create_publisher(msgs.SonarDetections, topic,
                               QoSProfile(depth=5, reliability=reliability))
    try:
        # Keep publishing until discovery has matched; the widget wants the newest.
        def delivered():
            pub.publish(make_msg())
            return len(widget.latest_profile()) == 7
        assert wait_for(delivered)
        assert all(abs(d - 20.0) < 1e-3 for _a, d in widget.latest_profile())
        assert widget.status_text()[0] == ''
    finally:
        widget.shutdown()
        ros.destroy_publisher(pub)
