# rqt_operator_tools

ROS 2 rqt plugins for operator station tools.

## Packages

- **rqt_annunciator** — Dark-until-problem annunciator panel for status monitoring
- **rqt_operator_log** — Operator logbook and checklist plugin
- **rqt_camera_grid** — Multi-stream `image_transport` grid view with per-pane staleness border (C++)
- **rqt_sonar_waterfall** — Scrolling sidescan backscatter waterfall for `marine_acoustic_msgs/RawSonarImage` (marine_colormap palettes, GPU path)
- **rqt_marine_sonar** — Water-column echogram ("curtain", depth vs. ping) for `marine_acoustic_msgs/RawSonarImage`; any sample dtype, marine_colormap palettes
- **rqt_boat_state** — Gauge/readout panel for boat state (odometry, mavros, sound speed)
- **rqt_marine_control** — Generic panel for marine_control device state/change topics
- **marine_control_widgets** — Shared Qt widgets for marine_control device controls (library)
- **marine_control_bridge_client** — udp_bridge-aware discovery/subscription client for remote marine_control devices (library)
