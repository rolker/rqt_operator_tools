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

#include "marine_control_bridge_client/bridge_control_client.hpp"

#include <algorithm>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace marine_control_bridge_client
{

BridgeControlClient::BridgeControlClient(
  rclcpp::Node * node, std::string bridge_node_name, std::string connection_id)
: node_(node),
  bridge_node_name_(std::move(bridge_node_name)),
  connection_id_(std::move(connection_id)),
  bridge_info_qos_(rclcpp::QoS(1).transient_local())
{
  remote_subscribe_client_ =
    node_->create_client<Subscribe>(bridge_node_name_ + "/remote_subscribe");
  remote_advertise_client_ =
    node_->create_client<Subscribe>(bridge_node_name_ + "/remote_advertise");
  remove_subscribe_client_ =
    node_->create_client<Subscribe>(bridge_node_name_ + "/remove_subscribe");
  remove_advertise_client_ =
    node_->create_client<Subscribe>(bridge_node_name_ + "/remove_advertise");

  local_bridge_info_sub_ = node_->create_subscription<BridgeInfo>(
    bridge_node_name_ + "/bridge_info", bridge_info_qos_,
    [this](const BridgeInfo::SharedPtr info) {onLocalBridgeInfo(info);});
}

std::string BridgeControlClient::deviceKey(
  const std::string & remote, const std::string & state_topic)
{
  return remote + '\n' + state_topic;
}

void BridgeControlClient::setDevicesChangedCallback(DevicesChangedCallback callback)
{
  std::lock_guard<std::mutex> lock(mutex_);
  devices_changed_callback_ = std::move(callback);
}

std::vector<ControlDevice> BridgeControlClient::availableDevices() const
{
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<ControlDevice> all;
  for (const auto & [remote, devices] : devices_by_remote_) {
    all.insert(all.end(), devices.begin(), devices.end());
  }
  std::sort(
    all.begin(), all.end(),
    [](const ControlDevice & a, const ControlDevice & b) {
      return std::tie(a.remote, a.state_topic) < std::tie(b.remote, b.state_topic);
    });
  return all;
}

bool BridgeControlClient::isConnected(
  const std::string & remote, const std::string & state_topic) const
{
  std::lock_guard<std::mutex> lock(mutex_);
  return connected_.count(deviceKey(remote, state_topic)) != 0;
}

void BridgeControlClient::connect(const ControlDevice & device)
{
  {
    std::lock_guard<std::mutex> lock(mutex_);
    connected_[deviceKey(device.remote, device.state_topic)] = device;
  }
  // state: ask the remote to push its ControlSet to us. change: push our local
  // ControlValue to the remote. The bridge's default per-topic QoS is already
  // RELIABLE + VOLATILE (ADR-0003 D5), so no QoS override is needed here. Wire
  // over every connection the remote offers (cell/vpn/wifi), so control keeps
  // working whichever link is up.
  callServiceAllConnections(remote_subscribe_client_, device.remote, device.state_topic, device.state_topic);
  callServiceAllConnections(remote_advertise_client_, device.remote, device.change_topic, device.change_topic);
}

void BridgeControlClient::disconnect(const ControlDevice & device)
{
  {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto key = deviceKey(device.remote, device.state_topic);
    connected_.erase(key);
    established_.erase(key);
  }
  callServiceAllConnections(remove_subscribe_client_, device.remote, device.state_topic, device.state_topic);
  callServiceAllConnections(remove_advertise_client_, device.remote, device.change_topic, device.change_topic);
}

void BridgeControlClient::callServiceAllConnections(
  const rclcpp::Client<Subscribe>::SharedPtr & client, const std::string & remote,
  const std::string & source_topic, const std::string & destination_topic)
{
  std::vector<std::string> connection_ids;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = connection_ids_by_remote_.find(remote);
    if (it != connection_ids_by_remote_.end()) {
      connection_ids = it->second;
    }
  }
  // The device was discovered from the same bridge_info that carries the
  // connection list, so this is normally populated. Fall back to the configured
  // default only if a race left it empty, and say so.
  if (connection_ids.empty()) {
    RCLCPP_WARN(
      node_->get_logger(),
      "marine_control bridge client: no connections known for remote '%s'; "
      "falling back to connection_id '%s'", remote.c_str(), connection_id_.c_str());
    connection_ids.push_back(connection_id_);
  }
  for (const auto & connection_id : connection_ids) {
    callService(client, remote, connection_id, source_topic, destination_topic);
  }
}

void BridgeControlClient::callService(
  const rclcpp::Client<Subscribe>::SharedPtr & client, const std::string & remote,
  const std::string & connection_id, const std::string & source_topic,
  const std::string & destination_topic)
{
  if (!client->service_is_ready()) {
    RCLCPP_WARN(
      node_->get_logger(),
      "marine_control bridge client: %s not available; '%s' request to '%s' dropped",
      client->get_service_name(), source_topic.c_str(), remote.c_str());
    return;
  }
  auto request = std::make_shared<Subscribe::Request>();
  request->remote = remote;
  request->connection_id = connection_id;
  request->source_topic = source_topic;
  request->destination_topic = destination_topic;
  request->queue_size = 10;
  request->period = 0.0f;  // no rate limit
  // Fire-and-forget: success is observed via bridge_info / the state echo (D1).
  client->async_send_request(
    request, [](rclcpp::Client<Subscribe>::SharedFuture) {});
}

void BridgeControlClient::onLocalBridgeInfo(const BridgeInfo::SharedPtr info)
{
  // Topics the local bridge is actually forwarding, paired with their remote, so
  // we can tell whether a connected device's change-advertise is still in place.
  std::set<std::pair<std::string, std::string>> bridged;
  for (const auto & topic : info->topics) {
    for (const auto & remote_detail : topic.remotes) {
      bridged.insert({topic.topic, remote_detail.remote});
    }
  }

  std::vector<std::pair<std::string, std::string>> new_remote_subs;  // (name, topic_name)
  std::vector<ControlDevice> to_reestablish;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto & remote : info->remotes) {
      // Record the connection_ids this remote offers, so connect()/disconnect()
      // can wire device topics over all of them (not a hardcoded "default").
      std::vector<std::string> ids;
      ids.reserve(remote.connections.size());
      for (const auto & connection : remote.connections) {
        ids.push_back(connection.connection_id);
      }
      connection_ids_by_remote_[remote.name] = std::move(ids);
      if (remote_bridge_info_subs_.find(remote.topic_name) == remote_bridge_info_subs_.end()) {
        new_remote_subs.push_back({remote.name, remote.topic_name});
      }
    }
    // Re-establish on an established->lost transition only: a device we have
    // seen actually bridged and now no longer see (bridge restarted). A
    // freshly-connected device isn't re-issued every tick before the bridge
    // registers its advertise (~1-2 ticks); the initial connect() handles setup.
    for (const auto & [key, device] : connected_) {
      const bool is_bridged = bridged.find({device.change_topic, device.remote}) != bridged.end();
      if (is_bridged) {
        established_.insert(key);
      } else if (established_.erase(key) > 0) {
        to_reestablish.push_back(device);  // was established, now gone
      }
    }
  }

  // create_subscription is an rmw call; do it off the lock, then record it.
  for (const auto & [name, topic_name] : new_remote_subs) {
    auto sub = node_->create_subscription<BridgeInfo>(
      bridge_node_name_ + "/remotes/" + topic_name + "/bridge_info", bridge_info_qos_,
      [this, name](const BridgeInfo::SharedPtr msg) {onRemoteBridgeInfo(name, msg);});
    std::lock_guard<std::mutex> lock(mutex_);
    remote_bridge_info_subs_[topic_name] = sub;
  }

  for (const auto & device : to_reestablish) {
    callServiceAllConnections(remote_subscribe_client_, device.remote, device.state_topic, device.state_topic);
    callServiceAllConnections(remote_advertise_client_, device.remote, device.change_topic, device.change_topic);
  }
}

void BridgeControlClient::onRemoteBridgeInfo(
  const std::string & remote_name, const BridgeInfo::SharedPtr info)
{
  auto devices = controlDevicesFromBridgeInfo(*info, remote_name);
  DevicesChangedCallback callback;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    devices_by_remote_[remote_name] = std::move(devices);
    callback = devices_changed_callback_;
  }
  if (callback) {
    callback();
  }
}

}  // namespace marine_control_bridge_client
