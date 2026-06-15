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

#include <gtest/gtest.h>

#include <string>

#include "udp_bridge_interfaces/msg/bridge_info.hpp"
#include "udp_bridge_interfaces/msg/topic_info.hpp"

#include "marine_control_bridge_client/bridge_control_discovery.hpp"

namespace
{

using marine_control_bridge_client::controlDevicesFromBridgeInfo;
using marine_control_bridge_client::deriveChangeTopic;
using marine_control_bridge_client::kControlSetDatatype;

udp_bridge_interfaces::msg::TopicInfo makeTopic(
  const std::string & name, const std::string & datatype)
{
  udp_bridge_interfaces::msg::TopicInfo t;
  t.topic = name;
  t.datatype = datatype;
  return t;
}

TEST(DeriveChangeTopic, TrailingStateBecomesChange)
{
  EXPECT_EQ(deriveChangeTopic("/sensors/sidescan/control/state"),
    "/sensors/sidescan/control/change");
}

TEST(DeriveChangeTopic, NoTrailingStateAppendsChange)
{
  EXPECT_EQ(deriveChangeTopic("/sensors/sidescan/control"),
    "/sensors/sidescan/control/change");
}

TEST(DeriveChangeTopic, TrailingSlashAppendsChangeWithoutDoubleSlash)
{
  EXPECT_EQ(deriveChangeTopic("/sensors/sidescan/"), "/sensors/sidescan/change");
}

TEST(DeriveChangeTopic, EmptyStaysEmpty)
{
  EXPECT_EQ(deriveChangeTopic(""), "");
}

TEST(ControlDevicesFromBridgeInfo, PicksOnlyControlSetTopics)
{
  udp_bridge_interfaces::msg::BridgeInfo info;
  info.topics.push_back(makeTopic("/b/control/state", kControlSetDatatype));
  info.topics.push_back(makeTopic("/sonar_image", "marine_acoustic_msgs/msg/RawSonarImage"));
  info.topics.push_back(makeTopic("/a/control/state", kControlSetDatatype));

  auto devices = controlDevicesFromBridgeInfo(info, "boat");
  ASSERT_EQ(devices.size(), 2u);
  // sorted by state topic
  EXPECT_EQ(devices[0].state_topic, "/a/control/state");
  EXPECT_EQ(devices[1].state_topic, "/b/control/state");
  // remote + derived change topic
  EXPECT_EQ(devices[0].remote, "boat");
  EXPECT_EQ(devices[0].change_topic, "/a/control/change");
  EXPECT_EQ(devices[1].change_topic, "/b/control/change");
}

TEST(ControlDevicesFromBridgeInfo, EmptyWhenNoControlSets)
{
  udp_bridge_interfaces::msg::BridgeInfo info;
  info.topics.push_back(makeTopic("/sonar_image", "marine_acoustic_msgs/msg/RawSonarImage"));
  info.topics.push_back(makeTopic("/diagnostics", "diagnostic_msgs/msg/DiagnosticArray"));

  EXPECT_TRUE(controlDevicesFromBridgeInfo(info, "boat").empty());
}

}  // namespace
