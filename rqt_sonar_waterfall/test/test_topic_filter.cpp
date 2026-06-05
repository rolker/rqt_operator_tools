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

#include "rqt_sonar_waterfall/topic_filter.hpp"

namespace
{

using rqt_sonar_waterfall::raw_sonar_image_topics;

const char * kRaw = "marine_acoustic_msgs/msg/RawSonarImage";

}  // namespace

TEST(TopicFilter, KeepsOnlyRawSonarImageTopicsSorted)
{
  std::map<std::string, std::vector<std::string>> graph{
    {"/zed/stbd", {kRaw}},
    {"/zed/port", {kRaw}},
    {"/camera/image", {"sensor_msgs/msg/Image"}},
    {"/clock", {"rosgraph_msgs/msg/Clock"}},
  };
  EXPECT_EQ(
    raw_sonar_image_topics(graph),
    (std::vector<std::string>{"/zed/port", "/zed/stbd"}));
}

TEST(TopicFilter, EmptyGraphYieldsEmpty)
{
  std::map<std::string, std::vector<std::string>> graph;
  EXPECT_TRUE(raw_sonar_image_topics(graph).empty());
}

TEST(TopicFilter, MultiTypeTopicMatchesIfAnyTypeMatches)
{
  std::map<std::string, std::vector<std::string>> graph{
    {"/mixed", {"std_msgs/msg/Header", kRaw}},
  };
  EXPECT_EQ(
    raw_sonar_image_topics(graph),
    (std::vector<std::string>{"/mixed"}));
}

TEST(TopicFilter, NoMatchYieldsEmpty)
{
  std::map<std::string, std::vector<std::string>> graph{
    {"/camera/image", {"sensor_msgs/msg/Image"}},
  };
  EXPECT_TRUE(raw_sonar_image_topics(graph).empty());
}
