#!/usr/bin/env python3
# Copyright 2026 University of New Hampshire
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
#    * Redistributions of source code must retain the above copyright
#      notice, this list of conditions and the following disclaimer.
#
#    * Redistributions in binary form must reproduce the above copyright
#      notice, this list of conditions and the following disclaimer in the
#      documentation and/or other materials provided with the distribution.
#
#    * Neither the name of the University of New Hampshire nor the names of its
#      contributors may be used to endorse or promote products derived from
#      this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
# LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.

"""
Publish synthetic marine_acoustic_msgs/RawSonarImage sidescan pings.

A standalone test source for rqt_sonar_waterfall (issue #39): no real sonar
required. Emits a port and a starboard channel (beam_count = 1, INT16) carrying
a nadir-bright decay, seabed speckle, and a slowly weaving bright target so the
waterfall has visible structure to verify orientation, scaling and color maps.

    ros2 run rqt_sonar_waterfall synthetic_sidescan.py
    ros2 run rqt_sonar_waterfall synthetic_sidescan.py --ros-args -p rate:=15.0
"""

import array
import math
import struct

from marine_acoustic_msgs.msg import RawSonarImage, SonarImageData
import rclpy
from rclpy.node import Node
from rclpy.qos import qos_profile_sensor_data


class SyntheticSidescan(Node):

    def __init__(self):
        super().__init__('synthetic_sidescan')
        self.declare_parameter('rate', 10.0)
        self.declare_parameter('samples', 512)
        self.declare_parameter('sample_rate', 7500.0)
        self.declare_parameter('sound_speed', 1500.0)
        self.declare_parameter('port_topic', 'sonar_image_port')
        self.declare_parameter('starboard_topic', 'sonar_image_starboard')

        self.samples = int(self.get_parameter('samples').value)
        self.sample_rate = float(self.get_parameter('sample_rate').value)
        self.sound_speed = float(self.get_parameter('sound_speed').value)
        rate = float(self.get_parameter('rate').value)
        if rate <= 0.0:
            raise ValueError(f'rate must be > 0 Hz, got {rate}')

        self.port_pub = self.create_publisher(
            RawSonarImage,
            self.get_parameter('port_topic').value, qos_profile_sensor_data)
        self.stbd_pub = self.create_publisher(
            RawSonarImage,
            self.get_parameter('starboard_topic').value, qos_profile_sensor_data)

        self.ping = 0
        self.timer = self.create_timer(1.0 / rate, self.tick)
        range_m = self.sound_speed * self.samples / (2.0 * self.sample_rate)
        self.get_logger().info(
            f'Publishing {self.samples}-sample pings at {rate} Hz '
            f'(~{range_m:.0f} m range) on '
            f"'{self.get_parameter('port_topic').value}' / "
            f"'{self.get_parameter('starboard_topic').value}'")

    def make_channel(self, phase):
        """One ping of INT16 samples (nadir -> far range) for one side."""
        values = []
        target = 0.35 + 0.18 * math.sin(self.ping * 0.04 + phase)
        for j in range(self.samples):
            r = j / self.samples
            base = 0.45 * math.exp(-3.0 * r)                       # nadir bright decay
            speckle = 0.08 * (1.0 + math.sin(j * 0.7 + self.ping * 0.3 + phase))
            tgt = 0.85 * math.exp(-((r - target) ** 2) / (2 * 0.0015))  # weaving target
            v = max(0.0, min(1.0, base + speckle + tgt))
            values.append(int(v * 30000))
        packed = struct.pack(f'<{self.samples}h', *values)
        return array.array('B', packed)

    def make_msg(self, phase):
        msg = RawSonarImage()
        msg.header.stamp = self.get_clock().now().to_msg()
        msg.header.frame_id = 'sonar'
        msg.sample_rate = self.sample_rate
        msg.samples_per_beam = self.samples
        msg.ping_info.sound_speed = self.sound_speed
        msg.image.dtype = SonarImageData.DTYPE_INT16
        msg.image.beam_count = 1
        msg.image.is_bigendian = False
        msg.image.data = self.make_channel(phase)
        return msg

    def tick(self):
        self.port_pub.publish(self.make_msg(0.0))
        self.stbd_pub.publish(self.make_msg(math.pi))
        self.ping += 1


def main():
    rclpy.init()
    node = SyntheticSidescan()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.destroy_node()
        if rclpy.ok():
            rclpy.shutdown()


if __name__ == '__main__':
    main()
