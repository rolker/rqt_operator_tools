"""Duck-typed stand-ins for ``marine_acoustic_msgs/SonarDetections``.

The model reads messages by attribute, so the logic tests need neither ROS nor
the built message package.
"""

import math
from types import SimpleNamespace as NS


def detections(twtt, angles, flags=None, sound_speed=1500.0, sec=100, nanosec=0):
    """A ping from travel times (s), rx angles (rad) and flag values (default all OK)."""
    if flags is None:
        flags = [0] * len(twtt)
    return NS(
        header=NS(stamp=NS(sec=sec, nanosec=nanosec), frame_id='mbes'),
        ping_info=NS(sound_speed=sound_speed),
        two_way_travel_times=list(twtt),
        rx_angles=list(angles),
        flags=[NS(flag=f) for f in flags],
    )


def flat_bottom(depth=20.0, beams=11, half_angle_deg=60.0, sound_speed=1500.0,
                flags=None):
    """A flat seafloor ``depth`` m below the transducer, beams port to starboard."""
    angles = [math.radians(-half_angle_deg + 2 * half_angle_deg * i / (beams - 1))
              for i in range(beams)]
    twtt = [2.0 * depth / math.cos(a) / sound_speed for a in angles]
    return detections(twtt, angles, flags, sound_speed)
