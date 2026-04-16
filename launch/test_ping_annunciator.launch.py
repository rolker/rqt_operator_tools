"""Launch the annunciator panel with the ping monitor test config.

The ping monitor must be started separately:
  ros2 launch network_tools ping_monitor.launch.py \
    config_file:=$(ros2 pkg prefix network_tools)/share/network_tools/config/test_ping_targets.yaml
"""

import os

from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch.actions import ExecuteProcess


def generate_launch_description():
    pkg_share = get_package_share_directory('rqt_operator_tools')
    config_file = os.path.join(pkg_share, 'config', 'test_ping_annunciator.yaml')

    return LaunchDescription([
        ExecuteProcess(
            cmd=[
                'ros2', 'run', 'rqt_operator_tools', 'annunciator',
                '--config', config_file,
            ],
            output='screen',
        ),
    ])
