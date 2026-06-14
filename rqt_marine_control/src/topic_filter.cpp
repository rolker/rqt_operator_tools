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

#include "rqt_marine_control/topic_filter.hpp"

#include <algorithm>
#include <map>
#include <string>
#include <vector>

namespace rqt_marine_control
{

std::vector<std::string> control_set_topics(
  const std::map<std::string, std::vector<std::string>> & topics)
{
  std::vector<std::string> out;
  for (const auto & [name, types] : topics) {
    for (const auto & type : types) {
      if (type == kControlSetType) {
        out.push_back(name);
        break;
      }
    }
  }
  std::sort(out.begin(), out.end());
  return out;
}

std::string derive_change_topic(const std::string & state_topic)
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
  // No trailing "state": append a "/change" leaf (avoid a double slash).
  if (!state_topic.empty() && state_topic.back() == '/') {
    return state_topic + kChange;
  }
  return state_topic + "/" + kChange;
}

}  // namespace rqt_marine_control
