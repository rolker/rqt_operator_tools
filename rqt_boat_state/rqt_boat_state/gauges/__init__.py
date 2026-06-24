"""QPainter gauge widgets for the boat-state panel.

Shared dark palette so every gauge matches the annunciator's dark-until-problem
look and the trend sparklines.
"""

from python_qt_binding.QtGui import QColor

# Shared dark palette.
BG_COLOR = QColor(30, 30, 30)
FACE_COLOR = QColor(45, 45, 45)
TICK_COLOR = QColor(120, 120, 120)
LABEL_COLOR = QColor(200, 200, 200)
ACTUAL_COLOR = QColor(120, 200, 255)   # live/actual value (blue)
COMMAND_COLOR = QColor(255, 180, 0)    # commanded ghost (amber)
COURSE_COLOR = QColor(80, 220, 160)    # course-over-ground arrow (teal)
COMMAND_STALE_COLOR = QColor(90, 90, 90)  # greyed commanded marker
WARN_COLOR = QColor(255, 180, 0)
ERROR_COLOR = QColor(220, 40, 40)
OK_COLOR = QColor(80, 180, 80)
