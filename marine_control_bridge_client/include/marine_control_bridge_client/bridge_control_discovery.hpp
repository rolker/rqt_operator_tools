// Copyright 2026 Center for Coastal and Ocean Mapping & NOAA-UNH Joint
// Hydrographic Center, University of New Hampshire
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Pure discovery helpers for the marine_control bridge client (ADR-0003 D7-dyn):
// turn a remote's udp_bridge BridgeInfo into the list of controllable devices
// (its marine_control ControlSet topics) it offers. No ROS runtime, no node —
// just message-in / data-out, so it is unit-testable.
#ifndef MARINE_CONTROL_BRIDGE_CLIENT__BRIDGE_CONTROL_DISCOVERY_HPP_
#define MARINE_CONTROL_BRIDGE_CLIENT__BRIDGE_CONTROL_DISCOVERY_HPP_

#include <algorithm>
#include <string>
#include <vector>

#include "udp_bridge_interfaces/msg/bridge_info.hpp"

namespace marine_control_bridge_client
{

// Datatype (as get_topic_names_and_types / BridgeInfo report it) that marks a
// marine_control device-state topic. Matches rqt_marine_control's kControlSetType.
inline constexpr const char * kControlSetDatatype =
  "marine_control_interfaces/msg/ControlSet";

// A controllable device discovered on a remote: its ControlSet state topic, the
// derived ControlValue change topic, and the remote bridge node that owns it.
struct ControlDevice
{
  std::string remote;        // remote udp_bridge node name (for service calls)
  std::string state_topic;   // ControlSet topic, as named on the remote
  std::string change_topic;  // derived ControlValue topic
};

// Derive the change topic from a state topic: a trailing "state" leaf becomes
// "change"; otherwise append a "/change" leaf. Mirrors
// rqt_marine_control::derive_change_topic so both sides agree on the pairing.
inline std::string deriveChangeTopic(const std::string & state_topic)
{
  if (state_topic.empty()) {
    return std::string();
  }
  static const std::string kState = "state";
  static const std::string kChange = "change";
  if (state_topic.size() >= kState.size() &&
    state_topic.compare(state_topic.size() - kState.size(), kState.size(), kState) == 0)
  {
    return state_topic.substr(0, state_topic.size() - kState.size()) + kChange;
  }
  if (state_topic.back() == '/') {
    return state_topic + kChange;
  }
  return state_topic + "/" + kChange;
}

// Extract the controllable devices a remote offers from its BridgeInfo: every
// topic whose datatype is ControlSet. Sorted by state topic for a stable UI.
inline std::vector<ControlDevice> controlDevicesFromBridgeInfo(
  const udp_bridge_interfaces::msg::BridgeInfo & info, const std::string & remote_name)
{
  std::vector<ControlDevice> devices;
  for (const auto & topic : info.topics) {
    if (topic.datatype == kControlSetDatatype) {
      devices.push_back({remote_name, topic.topic, deriveChangeTopic(topic.topic)});
    }
  }
  std::sort(
    devices.begin(), devices.end(),
    [](const ControlDevice & a, const ControlDevice & b) {return a.state_topic < b.state_topic;});
  return devices;
}

}  // namespace marine_control_bridge_client

#endif  // MARINE_CONTROL_BRIDGE_CLIENT__BRIDGE_CONTROL_DISCOVERY_HPP_
