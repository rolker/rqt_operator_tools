"""Duck-typed stand-ins for ``marine_interfaces/CoverageAssessment``.

The model reads messages by attribute, so the logic tests need neither ROS nor
the built message package.
"""

from types import SimpleNamespace as NS


def source(name='boat1', covered=0.0, meeting=0.0, unique=0.0):
    return NS(source=name, area_covered=covered, area_meeting_standard=meeting,
              area_unique=unique)


def assessment(area_id='a', total=1000.0, covered=600.0, meeting=300.0,
               sources=None, sec=100, nanosec=0, cell_size=1.0,
               std_name='demo', a=0.25, b=0.0075):
    return NS(
        header=NS(stamp=NS(sec=sec, nanosec=nanosec), frame_id='map'),
        area=NS(id=area_id, standard=NS(name=std_name, a=a, b=b)),
        cell_size=cell_size,
        area_total=total,
        area_covered=covered,
        area_meeting_standard=meeting,
        sources=list(sources if sources is not None else []),
    )
