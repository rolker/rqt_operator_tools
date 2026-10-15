"""Real rclpy: a latched assessment reaches the widget through the executor thread.

Covers what the stub-node tests cannot: the QoS actually matching a
transient_local publisher (late join), and the cross-thread hand-off from an
executor thread to the Qt thread.  Runs on a private DDS domain, offscreen.
"""

import os
import random
import threading
import time

import pytest

os.environ.setdefault('QT_QPA_PLATFORM', 'offscreen')

rclpy = pytest.importorskip('rclpy')
msgs = pytest.importorskip('marine_interfaces.msg')

from python_qt_binding.QtWidgets import QApplication  # noqa: E402
from rclpy.executors import SingleThreadedExecutor  # noqa: E402
from rclpy.qos import (  # noqa: E402
    DurabilityPolicy,
    HistoryPolicy,
    QoSProfile,
    ReliabilityPolicy,
)

from rqt_coverage.coverage_widget import CoverageWidget  # noqa: E402
from rqt_coverage.score_model import CoverageConfig  # noqa: E402


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
    domain = random.randint(100, 200)
    rclpy.init(domain_id=domain)
    node = rclpy.create_node('rqt_coverage_test')
    executor = SingleThreadedExecutor()
    executor.add_node(node)
    thread = threading.Thread(target=executor.spin, daemon=True)
    thread.start()
    yield node
    executor.shutdown()
    thread.join(timeout=2.0)
    node.destroy_node()
    rclpy.shutdown()


def test_latched_assessment_reaches_a_late_joining_widget(ros):
    app = QApplication.instance() or QApplication([])
    assert app
    qos = QoSProfile(reliability=ReliabilityPolicy.RELIABLE,
                     durability=DurabilityPolicy.TRANSIENT_LOCAL,
                     history=HistoryPolicy.KEEP_LAST, depth=100)
    pub = ros.create_publisher(msgs.CoverageAssessment, 'coverage_test_topic', qos)

    msg = msgs.CoverageAssessment()
    msg.area.id = 'lake'
    msg.area_total = 1000.0
    msg.area_covered = 600.0
    msg.area_meeting_standard = 300.0
    msg.cell_size = 1.0
    msg.header.stamp.sec = 7
    pub.publish(msg)                       # published BEFORE the widget exists

    widget = CoverageWidget(ros, config=CoverageConfig(topic='coverage_test_topic'))
    widget.resize(600, 300)
    try:
        assert wait_for(lambda: len(widget._tiles) == 1), widget._notice.text()
        tile = widget._tiles[0]
        assert tile.title_label.text() == 'lake'
        assert tile.covered_value.text() == '60.0 %'
        assert tile.spec_value.text() == '30.0 %'
    finally:
        widget.shutdown()


def test_qos_mismatch_with_a_volatile_publisher_is_reported(ros):
    app = QApplication.instance() or QApplication([])
    assert app
    qos = QoSProfile(reliability=ReliabilityPolicy.BEST_EFFORT,
                     durability=DurabilityPolicy.VOLATILE,
                     history=HistoryPolicy.KEEP_LAST, depth=10)
    ros.create_publisher(msgs.CoverageAssessment, 'coverage_test_mismatch', qos)
    widget = CoverageWidget(ros, config=CoverageConfig(topic='coverage_test_mismatch'))
    try:
        assert wait_for(lambda: 'QoS mismatch' in widget._notice.text()), \
            widget._notice.text()
    finally:
        widget.shutdown()


def test_topic_switch_while_the_executor_spins(ros):
    app = QApplication.instance() or QApplication([])
    assert app
    qos = QoSProfile(reliability=ReliabilityPolicy.RELIABLE,
                     durability=DurabilityPolicy.TRANSIENT_LOCAL,
                     history=HistoryPolicy.KEEP_LAST, depth=100)
    pub_a = ros.create_publisher(msgs.CoverageAssessment, 'coverage_test_a', qos)
    pub_b = ros.create_publisher(msgs.CoverageAssessment, 'coverage_test_b', qos)
    for pub, area in ((pub_a, 'from_a'), (pub_b, 'from_b')):
        msg = msgs.CoverageAssessment()
        msg.area.id = area
        msg.area_total = 100.0
        pub.publish(msg)
    widget = CoverageWidget(ros, config=CoverageConfig(topic='coverage_test_a'))
    try:
        assert wait_for(lambda: widget.seen_area_ids() == ['from_a'])
        widget.load_config(CoverageConfig(topic='coverage_test_b'))
        assert wait_for(lambda: widget.seen_area_ids() == ['from_b'])
    finally:
        widget.shutdown()
