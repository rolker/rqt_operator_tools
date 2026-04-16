"""Launch the annunciator panel with the diagnostic test publisher."""

import os

from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch.actions import ExecuteProcess
from launch_ros.actions import Node


def generate_launch_description():
    pkg_share = get_package_share_directory('rqt_operator_tools')
    config_file = os.path.join(pkg_share, 'config', 'test_annunciator.yaml')

    return LaunchDescription([
        Node(
            package='rqt_operator_tools',
            executable='diagnostic_test_publisher',
            name='diagnostic_test_publisher',
        ),
        ExecuteProcess(
            cmd=[
                'ros2', 'run', 'rqt_operator_tools', 'annunciator',
                '--config', config_file,
            ],
            output='screen',
        ),
    ])
