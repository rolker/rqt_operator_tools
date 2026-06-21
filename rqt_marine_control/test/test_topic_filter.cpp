// Copyright 2026 University of New Hampshire
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
//    * Redistributions of source code must retain the above copyright
//      notice, this list of conditions and the following disclaimer.
//
//    * Redistributions in binary form must reproduce the above copyright
//      notice, this list of conditions and the following disclaimer in the
//      documentation and/or other materials provided with the distribution.
//
//    * Neither the name of the University of New Hampshire nor the names of its
//      contributors may be used to endorse or promote products derived from
//      this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.

#include <gtest/gtest.h>

#include <map>
#include <string>
#include <vector>

#include "rqt_marine_control/topic_filter.hpp"

namespace
{
using rqt_marine_control::bridge_nodes_from_services;
using rqt_marine_control::control_set_topics;
using rqt_marine_control::derive_change_topic;
}  // namespace

TEST(TopicFilter, SelectsControlSetTopicsSorted)
{
  std::map<std::string, std::vector<std::string>> topics = {
    {"/b/state", {"marine_control_interfaces/msg/ControlSet"}},
    {"/a/state", {"marine_control_interfaces/msg/ControlSet"}},
    {"/img", {"marine_acoustic_msgs/msg/RawSonarImage"}},
    {"/multi", {"std_msgs/msg/String", "marine_control_interfaces/msg/ControlSet"}},
  };
  const auto out = control_set_topics(topics);
  ASSERT_EQ(out.size(), 3u);
  EXPECT_EQ(out[0], "/a/state");      // sorted
  EXPECT_EQ(out[1], "/b/state");
  EXPECT_EQ(out[2], "/multi");        // multi-type topic still offered
}

TEST(TopicFilter, IgnoresNonControlTopics)
{
  std::map<std::string, std::vector<std::string>> topics = {
    {"/img", {"marine_acoustic_msgs/msg/RawSonarImage"}},
  };
  EXPECT_TRUE(control_set_topics(topics).empty());
}

TEST(TopicFilter, BridgeNodesFromServicesStripsSuffixSortedUnique)
{
  std::map<std::string, std::vector<std::string>> services = {
    {"/operator/udp_bridge/remote_subscribe", {"udp_bridge_interfaces/srv/Subscribe"}},
    {"/operator/udp_bridge/remote_advertise", {"udp_bridge_interfaces/srv/Subscribe"}},
    {"/operator/udp_bridge/list_remotes", {"udp_bridge_interfaces/srv/ListRemotes"}},
    {"/aaa/udp_bridge/remote_subscribe", {"udp_bridge_interfaces/srv/Subscribe"}},
    {"/some/other_service", {"std_srvs/srv/Trigger"}},
  };
  const auto out = bridge_nodes_from_services(services);
  ASSERT_EQ(out.size(), 2u);
  EXPECT_EQ(out[0], "/aaa/udp_bridge");        // sorted
  EXPECT_EQ(out[1], "/operator/udp_bridge");   // only the remote_subscribe service marks a bridge
}

TEST(TopicFilter, BridgeNodesFromServicesEmptyWhenNone)
{
  std::map<std::string, std::vector<std::string>> services = {
    {"/some/other_service", {"std_srvs/srv/Trigger"}},
  };
  EXPECT_TRUE(bridge_nodes_from_services(services).empty());
}

TEST(TopicFilter, DeriveChangeReplacesTrailingState)
{
  EXPECT_EQ(derive_change_topic("/sonar/state"), "/sonar/change");
  EXPECT_EQ(derive_change_topic("state"), "change");
  EXPECT_EQ(derive_change_topic("/bizzy/sensors/sidescan/state"),
    "/bizzy/sensors/sidescan/change");
}

TEST(TopicFilter, DeriveChangeAppendsWhenNoTrailingState)
{
  EXPECT_EQ(derive_change_topic("/sonar/controls"), "/sonar/controls/change");
  EXPECT_EQ(derive_change_topic("/sonar/"), "/sonar/change");   // no double slash
}

TEST(TopicFilter, DeriveChangeEmptyStaysEmpty)
{
  EXPECT_EQ(derive_change_topic(""), "");
}
