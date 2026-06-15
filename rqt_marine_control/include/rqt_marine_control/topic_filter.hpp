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

#ifndef RQT_MARINE_CONTROL__TOPIC_FILTER_HPP_
#define RQT_MARINE_CONTROL__TOPIC_FILTER_HPP_

#include <map>
#include <string>
#include <vector>

namespace rqt_marine_control
{

/// The control-state message type a controllable device advertises.
inline constexpr const char * kControlSetType =
  "marine_control_interfaces/msg/ControlSet";

/// From a node-graph topic->types map, return the sorted names of topics that
/// publish a ControlSet. A topic qualifies if any of its advertised types
/// matches, so multi-type topics are still offered.
std::vector<std::string> control_set_topics(
  const std::map<std::string, std::vector<std::string>> & topics);

/// Derive the change (command) topic from a state topic, per ADR-0003's
/// state/change convention: a trailing "state" becomes "change"
/// (e.g. "/sonar/state" -> "/sonar/change"); otherwise "/change" is appended.
/// An empty input yields an empty result.
std::string derive_change_topic(const std::string & state_topic);

/// The udp_bridge service every bridge node advertises to accept a
/// pull-a-remote-topic request; its presence marks a usable bridge node.
inline constexpr const char * kRemoteSubscribeService = "/remote_subscribe";

/// From a node-graph service->types map, return the sorted, de-duplicated
/// fully-qualified names of udp_bridge nodes (those advertising a
/// "<node>/remote_subscribe" service). Used to populate the bridge selector for
/// dynamic device control (ADR-0003 D7-dyn).
std::vector<std::string> bridge_nodes_from_services(
  const std::map<std::string, std::vector<std::string>> & services);

}  // namespace rqt_marine_control

#endif  // RQT_MARINE_CONTROL__TOPIC_FILTER_HPP_
