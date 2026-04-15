# Copyright 2024 Roland Arsenault
#
# Use of this source code is governed by a BSD-style
# license that can be found in the LICENSE file or at
# https://developers.google.com/open-source/licenses/bsd

"""Publish synthetic diagnostics that cycle through staggered status states.

Useful for testing the annunciator panel without any real ROS system.
Each indicator cycles through a four-step sequence (OK, OK, WARN, ERROR)
on a staggered schedule so the annunciator always shows a mix of states.
"""

from diagnostic_msgs.msg import DiagnosticArray, DiagnosticStatus, KeyValue
import rclpy
from rclpy.node import Node


# Synthetic indicators: (name, hardware_id, cycle of (level, message, kvs))
_INDICATORS = [
    (
        'GPS',
        'gps.test',
        [
            (DiagnosticStatus.OK, '3D Fix, 12 sats',
             [('status', '3D Fix'), ('fix_type', '3'), ('satellites', '12')]),
            (DiagnosticStatus.OK, '3D Fix, 8 sats',
             [('status', '3D Fix'), ('fix_type', '3'), ('satellites', '8')]),
            (DiagnosticStatus.WARN, '2D Fix, 4 sats',
             [('status', '2D Fix'), ('fix_type', '2'), ('satellites', '4')]),
            (DiagnosticStatus.ERROR, 'No Fix',
             [('status', 'No Fix'), ('fix_type', '0'), ('satellites', '0')]),
        ],
    ),
    (
        'Battery',
        'battery.test',
        [
            (DiagnosticStatus.OK, '13.2V',
             [('voltage', '13.2')]),
            (DiagnosticStatus.OK, '12.8V',
             [('voltage', '12.8')]),
            (DiagnosticStatus.WARN, '12.1V',
             [('voltage', '12.1')]),
            (DiagnosticStatus.ERROR, '11.2V',
             [('voltage', '11.2')]),
        ],
    ),
    (
        'Comms',
        'comms.test',
        [
            (DiagnosticStatus.OK, 'Connected',
             [('latency_ms', '15.2')]),
            (DiagnosticStatus.OK, 'Connected',
             [('latency_ms', '42.7')]),
            (DiagnosticStatus.WARN, 'High latency',
             [('latency_ms', '312.5')]),
            (DiagnosticStatus.ERROR, 'Timeout',
             [('latency_ms', '0.0')]),
        ],
    ),
    (
        'Heartbeat',
        'heartbeat.test',
        [
            (DiagnosticStatus.OK, 'Active',
             [('rate_hz', '10.0')]),
            (DiagnosticStatus.OK, 'Active',
             [('rate_hz', '9.8')]),
            (DiagnosticStatus.WARN, 'Slow',
             [('rate_hz', '3.2')]),
            (DiagnosticStatus.ERROR, 'Missing',
             [('rate_hz', '0.0')]),
        ],
    ),
]


class DiagnosticTestPublisher(Node):

    def __init__(self):
        super().__init__('diagnostic_test_publisher')

        self.declare_parameter('publish_interval', 2.0)
        self.declare_parameter('name_prefix', 'test')

        self._interval = self.get_parameter('publish_interval').value
        self._prefix = self.get_parameter('name_prefix').value

        self._pub = self.create_publisher(
            DiagnosticArray, '/diagnostics', 10
        )

        self._cycle_index = 0
        self._timer = self.create_timer(self._interval, self._publish)

        self.get_logger().info(
            f'Diagnostic test publisher started, '
            f'publishing every {self._interval}s'
        )

    def _publish(self):
        msg = DiagnosticArray()
        msg.header.stamp = self.get_clock().now().to_msg()

        for i, (name, hw_id, cycle) in enumerate(_INDICATORS):
            # Stagger each indicator so they're not all in the same state.
            idx = (self._cycle_index + i) % len(cycle)
            level, message, kvs = cycle[idx]

            status = DiagnosticStatus()
            status.name = f'{self._prefix}: {name}'
            status.hardware_id = hw_id
            status.level = level
            status.message = message
            for k, v in kvs:
                status.values.append(KeyValue(key=k, value=v))

            msg.status.append(status)

        self._pub.publish(msg)
        self._cycle_index += 1


def main(args=None):
    rclpy.init(args=args)
    node = DiagnosticTestPublisher()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.destroy_node()
        rclpy.try_shutdown()
