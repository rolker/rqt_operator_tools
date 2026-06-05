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

#ifndef RQT_SONAR_WATERFALL__TOPIC_FILTER_HPP_
#define RQT_SONAR_WATERFALL__TOPIC_FILTER_HPP_

#include <map>
#include <string>
#include <vector>

namespace rqt_sonar_waterfall
{

/// The fully-qualified message type this viewer subscribes to.
inline constexpr const char * kRawSonarImageType =
  "marine_acoustic_msgs/msg/RawSonarImage";

/// The control-state message type (advertised by a controllable sonar).
inline constexpr const char * kRadarControlSetType =
  "marine_radar_control_msgs/msg/RadarControlSet";

/// From a node-graph topic->types map, return the sorted names of topics that
/// publish RawSonarImage. A topic qualifies if any of its advertised types
/// matches, so multi-type topics are still offered.
std::vector<std::string> raw_sonar_image_topics(
  const std::map<std::string, std::vector<std::string>> & topics);

/// As raw_sonar_image_topics(), but for RadarControlSet control-state topics.
std::vector<std::string> radar_control_set_topics(
  const std::map<std::string, std::vector<std::string>> & topics);

/// Derive the change-state (command) topic from a control-state topic, matching
/// the marine_radar convention: a trailing "state" becomes "change_state"
/// (e.g. "/sonar/state" -> "/sonar/change_state"); otherwise "/change_state" is
/// appended. An empty input yields an empty result.
std::string derive_change_topic(const std::string & state_topic);

}  // namespace rqt_sonar_waterfall

#endif  // RQT_SONAR_WATERFALL__TOPIC_FILTER_HPP_
