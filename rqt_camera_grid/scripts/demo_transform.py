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
Demo transform node for rqt_camera_grid.

Subscribes to `input/image_raw`, applies an OpenCV transform selected via the
`transform` parameter, and republishes on `output/image_raw`. Used by
demo_webcam_grid.launch.py to fan a single webcam out into several visually
distinct streams for demo and user-testing purposes. Supported values for the
`transform` parameter: none, flip_h, flip_v, grayscale, crop_left, crop_right,
blur, edge, negative.
"""

import sys

import cv2
from cv_bridge import CvBridge
import rclpy
from rclpy.node import Node
from sensor_msgs.msg import Image


_TRANSFORMS = {
    'none',
    'flip_h',
    'flip_v',
    'grayscale',
    'crop_left',
    'crop_right',
    'blur',
    'edge',
    'negative',
}


class DemoTransform(Node):
    """ROS 2 node that applies a configurable OpenCV transform per image."""

    def __init__(self) -> None:
        super().__init__('demo_transform')
        self.declare_parameter('transform', 'none')
        self._transform = (
            self.get_parameter('transform').get_parameter_value().string_value
        )
        if self._transform not in _TRANSFORMS:
            self.get_logger().warn(
                f'unknown transform "{self._transform}"; falling back to pass-through'
            )
            self._transform = 'none'
        self._bridge = CvBridge()
        # Remap input/image_raw and output/image_raw in the launch file.
        self._sub = self.create_subscription(
            Image, 'input/image_raw', self._on_image, 10
        )
        self._pub = self.create_publisher(Image, 'output/image_raw', 10)
        self.get_logger().info(f'demo_transform ready: {self._transform}')

    def _on_image(self, msg: Image) -> None:
        try:
            frame = self._bridge.imgmsg_to_cv2(msg, desired_encoding='rgb8')
        except Exception as exc:  # cv_bridge.CvBridgeError or similar
            self.get_logger().warn(f'cv_bridge conversion failed: {exc}')
            return
        transformed = self._apply(frame)
        out = self._bridge.cv2_to_imgmsg(transformed, encoding='rgb8')
        out.header = msg.header
        self._pub.publish(out)

    def _apply(self, frame):
        t = self._transform
        if t == 'flip_h':
            return cv2.flip(frame, 1)
        if t == 'flip_v':
            return cv2.flip(frame, 0)
        if t == 'grayscale':
            gray = cv2.cvtColor(frame, cv2.COLOR_RGB2GRAY)
            return cv2.cvtColor(gray, cv2.COLOR_GRAY2RGB)
        if t == 'crop_left':
            _, w = frame.shape[:2]
            return frame[:, : w // 2].copy()
        if t == 'crop_right':
            _, w = frame.shape[:2]
            return frame[:, w // 2:].copy()
        if t == 'blur':
            return cv2.GaussianBlur(frame, (21, 21), 0)
        if t == 'edge':
            gray = cv2.cvtColor(frame, cv2.COLOR_RGB2GRAY)
            edges = cv2.Canny(gray, 100, 200)
            return cv2.cvtColor(edges, cv2.COLOR_GRAY2RGB)
        if t == 'negative':
            return 255 - frame
        return frame


def main(args=None) -> int:
    rclpy.init(args=args)
    node = DemoTransform()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.destroy_node()
        if rclpy.ok():
            rclpy.shutdown()
    return 0


if __name__ == '__main__':
    sys.exit(main())
