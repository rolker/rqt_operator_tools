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
Demo launch: one local webcam fanned into six visually distinct streams.

Runs v4l2_camera on /dev/video0 (override with `video_device:=/dev/videoN`)
and five demo_transform.py instances, each applying a different OpenCV
effect. Together with the raw webcam stream this produces the six sources
that populate the 2x3 grid in config/demo_webcam_grid.yaml — useful for
hands-on testing of rqt_camera_grid without needing real cameras.
Launch arguments: video_device (default /dev/video0), image_size (default
"[640, 480]"). See package README "Demo" section for rqt usage.
"""

from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node


_TRANSFORMS = ('flip_h', 'crop_left', 'crop_right', 'grayscale', 'negative')


def _transform_node(name: str) -> Node:
    """
    Build one demo_transform Node in namespace demo/<name>.

    Subscribes to /demo/webcam/image_raw, publishes to /demo/<name>/image_raw.
    """
    return Node(
        package='rqt_camera_grid',
        executable='demo_transform.py',
        name=name,
        namespace=f'demo/{name}',
        output='screen',
        parameters=[{'transform': name}],
        remappings=[
            ('input/image_raw', '/demo/webcam/image_raw'),
            ('output/image_raw', 'image_raw'),
        ],
    )


def generate_launch_description() -> LaunchDescription:
    video_device = LaunchConfiguration('video_device')
    image_size = LaunchConfiguration('image_size')

    return LaunchDescription([
        DeclareLaunchArgument(
            'video_device',
            default_value='/dev/video0',
            description='V4L2 device node for the source webcam.',
        ),
        DeclareLaunchArgument(
            'image_size',
            default_value='[640, 480]',
            description='Capture size as "[width, height]".',
        ),

        Node(
            package='v4l2_camera',
            executable='v4l2_camera_node',
            name='webcam',
            namespace='demo/webcam',
            output='screen',
            parameters=[{
                'video_device': video_device,
                'image_size': image_size,
                'pixel_format': 'YUYV',
                'output_encoding': 'rgb8',
            }],
        ),
        *(_transform_node(t) for t in _TRANSFORMS),
    ])
