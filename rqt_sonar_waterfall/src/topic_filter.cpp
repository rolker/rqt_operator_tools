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

#include "rqt_sonar_waterfall/topic_filter.hpp"

#include <algorithm>
#include <string>

namespace rqt_sonar_waterfall
{

namespace
{
std::vector<std::string> topics_of_type(
  const std::map<std::string, std::vector<std::string>> & topics, const char * type)
{
  std::vector<std::string> names;
  for (const auto & [name, types] : topics) {
    if (std::find(types.begin(), types.end(), type) != types.end()) {
      names.push_back(name);
    }
  }
  std::sort(names.begin(), names.end());  // std::map already sorts, but be explicit
  return names;
}
}  // namespace

std::vector<std::string> raw_sonar_image_topics(
  const std::map<std::string, std::vector<std::string>> & topics)
{
  return topics_of_type(topics, kRawSonarImageType);
}

std::vector<std::string> radar_control_set_topics(
  const std::map<std::string, std::vector<std::string>> & topics)
{
  return topics_of_type(topics, kRadarControlSetType);
}

std::vector<std::string> range_topics(
  const std::map<std::string, std::vector<std::string>> & topics)
{
  return topics_of_type(topics, kRangeType);
}

std::string derive_change_topic(const std::string & state_topic)
{
  if (state_topic.empty()) {
    return std::string();
  }
  // Only rewrite a final "/state" path segment, not any trailing "state"
  // substring (e.g. ".../estate" must not become ".../echange_state").
  const std::string segment = "/state";
  if (state_topic.size() >= segment.size() &&
    state_topic.compare(state_topic.size() - segment.size(), segment.size(), segment) == 0)
  {
    return state_topic.substr(0, state_topic.size() - segment.size()) + "/change_state";
  }
  return state_topic + "/change_state";
}

}  // namespace rqt_sonar_waterfall
