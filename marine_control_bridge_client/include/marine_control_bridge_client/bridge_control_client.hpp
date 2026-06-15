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

// Operator-side client for marine_control's dynamic bridge wiring (ADR-0003
// D7-dyn). It DISCOVERS controllable devices passively from udp_bridge's
// bridge_info, and on an EXPLICIT connect()/disconnect() request wires (or tears
// down) the device's state/change topics over the local udp_bridge via its
// remote_subscribe/advertise + remove_subscribe/advertise services. No Qt — a UI
// (rqt_marine_control) drives it and renders the discovered list.
//
// Threading: designed for a SINGLE-THREADED executor (rqt's model). The
// bridge_info callbacks and the public connect()/disconnect()/query methods are
// each individually mutex-guarded, but the client is not hardened against a
// multi-threaded executor (e.g. two concurrent bridge_info callbacks could both
// create a per-remote subscription). The borrowed node must outlive the client,
// and the client must be destroyed while the node is not spinning (so no
// callback is mid-flight against a half-destroyed client).
#ifndef MARINE_CONTROL_BRIDGE_CLIENT__BRIDGE_CONTROL_CLIENT_HPP_
#define MARINE_CONTROL_BRIDGE_CLIENT__BRIDGE_CONTROL_CLIENT_HPP_

#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "rclcpp/rclcpp.hpp"

#include "udp_bridge_interfaces/msg/bridge_info.hpp"
#include "udp_bridge_interfaces/srv/subscribe.hpp"

#include "marine_control_bridge_client/bridge_control_discovery.hpp"

namespace marine_control_bridge_client
{

class BridgeControlClient
{
public:
  // Called (on the executor thread) whenever the discovered device list or a
  // device's connected state changes, so a UI can refresh.
  using DevicesChangedCallback = std::function<void()>;

  // node: any rclcpp::Node the client borrows for pub/sub/service-client and
  // logging (must outlive the client). bridge_node_name: the local udp_bridge
  // node's fully-qualified name, e.g. "/operator/udp_bridge". connection_id: the
  // udp_bridge connection to wire device topics over.
  BridgeControlClient(
    rclcpp::Node * node, std::string bridge_node_name, std::string connection_id = "default");

  // Snapshot of all discovered devices across remotes, sorted by
  // (remote, state topic). Devices on different remotes that share a state-topic
  // name are distinct entries (distinguished by remote), not duplicates.
  std::vector<ControlDevice> availableDevices() const;

  // Whether the operator has connected this device (its bridges are set up).
  bool isConnected(const std::string & remote, const std::string & state_topic) const;

  // Explicitly wire a device's state (boat->operator) and change
  // (operator->boat) topics. Idempotent; re-issued automatically if the bridge
  // restarts while connected.
  void connect(const ControlDevice & device);

  // Explicitly tear down a device's bridges.
  void disconnect(const ControlDevice & device);

  void setDevicesChangedCallback(DevicesChangedCallback callback);

private:
  using Subscribe = udp_bridge_interfaces::srv::Subscribe;
  using BridgeInfo = udp_bridge_interfaces::msg::BridgeInfo;

  void onLocalBridgeInfo(const BridgeInfo::SharedPtr info);
  void onRemoteBridgeInfo(const std::string & remote_name, const BridgeInfo::SharedPtr info);
  // Issue one Subscribe-shaped request (subscribe/advertise/remove) to a service.
  void callService(
    const rclcpp::Client<Subscribe>::SharedPtr & client, const std::string & remote,
    const std::string & source_topic, const std::string & destination_topic);
  static std::string deviceKey(const std::string & remote, const std::string & state_topic);

  rclcpp::Node * node_;
  std::string bridge_node_name_;
  std::string connection_id_;

  rclcpp::QoS bridge_info_qos_;  // latched: transient_local, KeepLast(1)
  rclcpp::Subscription<BridgeInfo>::SharedPtr local_bridge_info_sub_;
  // Per-remote bridge_info subscriptions, keyed by the remote's topic_name.
  std::map<std::string, rclcpp::Subscription<BridgeInfo>::SharedPtr> remote_bridge_info_subs_;

  rclcpp::Client<Subscribe>::SharedPtr remote_subscribe_client_;
  rclcpp::Client<Subscribe>::SharedPtr remote_advertise_client_;
  rclcpp::Client<Subscribe>::SharedPtr remove_subscribe_client_;
  rclcpp::Client<Subscribe>::SharedPtr remove_advertise_client_;

  mutable std::mutex mutex_;
  std::map<std::string, std::vector<ControlDevice>> devices_by_remote_;  // discovered
  std::map<std::string, ControlDevice> connected_;                       // key -> device
  // Connected devices observed actually bridged at least once since their last
  // (re)connect. Re-establish fires only on an established->lost transition, so a
  // freshly-issued connect isn't re-issued every tick before the bridge reflects
  // it (the bridge takes ~1-2 ticks to register the advertise in bridge_info).
  std::set<std::string> established_;

  DevicesChangedCallback devices_changed_callback_;
};

}  // namespace marine_control_bridge_client

#endif  // MARINE_CONTROL_BRIDGE_CLIENT__BRIDGE_CONTROL_CLIENT_HPP_
