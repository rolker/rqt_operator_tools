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

#include "rqt_camera_grid/config_model.hpp"

using rqt_camera_grid::ConfigParseError;
using rqt_camera_grid::GridConfig;
using rqt_camera_grid::PaneConfig;
using rqt_camera_grid::config_from_yaml;
using rqt_camera_grid::config_to_yaml;
using rqt_camera_grid::parse_image_topics;
using rqt_camera_grid::resize_panes;

// --- YAML roundtrip ---------------------------------------------------------

TEST(ConfigModel, YamlRoundtripEmpty)
{
  GridConfig c;
  c.rows = 2;
  c.cols = 2;
  const std::string yaml = config_to_yaml(c);
  GridConfig parsed = config_from_yaml(yaml);
  EXPECT_EQ(parsed, c);
}

TEST(ConfigModel, YamlRoundtripWithPanes)
{
  GridConfig c;
  c.rows = 2;
  c.cols = 2;
  c.panes = {
    {"/a/image_raw", "ffmpeg", 2.0, 5.0},
    {"/b/image_raw", "compressed", 1.5, 4.0},
    {"/c", "raw", 2.0, 5.0},
    {"/d", "raw", 2.0, 5.0},
  };
  const std::string yaml = config_to_yaml(c);
  GridConfig parsed = config_from_yaml(yaml);
  EXPECT_EQ(parsed, c);
}

// --- Parse errors ----------------------------------------------------------

TEST(ConfigModel, ParseErrorOnMalformedYaml)
{
  EXPECT_THROW(config_from_yaml("::::not yaml::::"), ConfigParseError);
}

TEST(ConfigModel, ParseErrorOnNonMapTopLevel)
{
  EXPECT_THROW(config_from_yaml("- just\n- a\n- list\n"), ConfigParseError);
  EXPECT_THROW(config_from_yaml("42\n"), ConfigParseError);
}

TEST(ConfigModel, ParseErrorOnMissingGrid)
{
  EXPECT_THROW(config_from_yaml("panes: []\n"), ConfigParseError);
}

TEST(ConfigModel, ParseErrorOnMissingRowsCols)
{
  EXPECT_THROW(config_from_yaml("grid: {}\n"), ConfigParseError);
  EXPECT_THROW(config_from_yaml("grid: {rows: 2}\n"), ConfigParseError);
  EXPECT_THROW(config_from_yaml("grid: {cols: 2}\n"), ConfigParseError);
}

TEST(ConfigModel, ParseErrorOnZeroOrNegativeRowsCols)
{
  EXPECT_THROW(config_from_yaml("grid: {rows: 0, cols: 2}\n"), ConfigParseError);
  EXPECT_THROW(config_from_yaml("grid: {rows: 2, cols: -1}\n"), ConfigParseError);
}

TEST(ConfigModel, ParseErrorOnNonIntRows)
{
  EXPECT_THROW(
    config_from_yaml("grid: {rows: two, cols: 2}\n"), ConfigParseError);
}

TEST(ConfigModel, ParseErrorOnNonSequencePanes)
{
  EXPECT_THROW(
    config_from_yaml("grid: {rows: 1, cols: 1}\npanes: not_a_list\n"),
    ConfigParseError);
}

TEST(ConfigModel, ParseErrorOnPaneMissingBase)
{
  EXPECT_THROW(
    config_from_yaml(
      "grid: {rows: 1, cols: 1}\npanes:\n  - {transport: raw}\n"),
    ConfigParseError);
}

TEST(ConfigModel, ParseErrorOnUnknownTransport)
{
  EXPECT_THROW(
    config_from_yaml(
      "grid: {rows: 1, cols: 1}\n"
      "panes:\n  - {base: /x, transport: bogus}\n"),
    ConfigParseError);
}

TEST(ConfigModel, ParseErrorOnNonNumericThreshold)
{
  EXPECT_THROW(
    config_from_yaml(
      "grid: {rows: 1, cols: 1}\n"
      "panes:\n  - {base: /x, warn_s: soon}\n"),
    ConfigParseError);
}

TEST(ConfigModel, ParseErrorOnNegativeWarnS)
{
  EXPECT_THROW(
    config_from_yaml(
      "grid: {rows: 1, cols: 1}\n"
      "panes:\n  - {base: /x, warn_s: -1.0, error_s: 5.0}\n"),
    ConfigParseError);
}

TEST(ConfigModel, ParseErrorOnNegativeErrorS)
{
  EXPECT_THROW(
    config_from_yaml(
      "grid: {rows: 1, cols: 1}\n"
      "panes:\n  - {base: /x, warn_s: 1.0, error_s: -2.0}\n"),
    ConfigParseError);
}

TEST(ConfigModel, ParseErrorOnReversedThresholds)
{
  EXPECT_THROW(
    config_from_yaml(
      "grid: {rows: 1, cols: 1}\n"
      "panes:\n  - {base: /x, warn_s: 5.0, error_s: 1.0}\n"),
    ConfigParseError);
}

TEST(ConfigModel, MissingPanesKeyOkYieldsEmpty)
{
  GridConfig c = config_from_yaml("grid: {rows: 2, cols: 2}\n");
  EXPECT_EQ(c.rows, 2);
  EXPECT_EQ(c.cols, 2);
  EXPECT_TRUE(c.panes.empty());
}

TEST(ConfigModel, DefaultThresholdsAppliedWhenOmitted)
{
  GridConfig c = config_from_yaml(
    "grid: {rows: 1, cols: 1}\n"
    "panes:\n  - {base: /x, transport: raw}\n");
  ASSERT_EQ(c.panes.size(), 1u);
  EXPECT_DOUBLE_EQ(c.panes[0].warn_s, 2.0);
  EXPECT_DOUBLE_EQ(c.panes[0].error_s, 5.0);
}

// --- resize_panes matrix ---------------------------------------------------

TEST(ConfigModel, ResizePanesGrow)
{
  GridConfig c;
  c.rows = 1;
  c.cols = 1;
  c.panes = {{"/keep", "raw", 2.0, 5.0}};
  resize_panes(c, 2, 2);
  EXPECT_EQ(c.rows, 2);
  EXPECT_EQ(c.cols, 2);
  ASSERT_EQ(c.panes.size(), 4u);
  EXPECT_EQ(c.panes[0].base, "/keep");
  EXPECT_TRUE(c.panes[1].base.empty());
  EXPECT_TRUE(c.panes[2].base.empty());
  EXPECT_TRUE(c.panes[3].base.empty());
}

TEST(ConfigModel, ResizePanesShrinkTruncatesTail)
{
  GridConfig c;
  c.rows = 2;
  c.cols = 2;
  c.panes = {
    {"/a", "raw", 2.0, 5.0},
    {"/b", "raw", 2.0, 5.0},
    {"/c", "raw", 2.0, 5.0},
    {"/d", "raw", 2.0, 5.0},
  };
  resize_panes(c, 1, 2);
  EXPECT_EQ(c.rows, 1);
  EXPECT_EQ(c.cols, 2);
  ASSERT_EQ(c.panes.size(), 2u);
  EXPECT_EQ(c.panes[0].base, "/a");
  EXPECT_EQ(c.panes[1].base, "/b");
}

TEST(ConfigModel, ResizePanesClampsZeroDims)
{
  GridConfig c;
  resize_panes(c, 0, -5);
  EXPECT_EQ(c.rows, 1);
  EXPECT_EQ(c.cols, 1);
  EXPECT_EQ(c.panes.size(), 1u);
}

TEST(ConfigModel, ResizePanesIdempotentWhenSizeMatches)
{
  GridConfig c;
  c.rows = 2;
  c.cols = 2;
  c.panes.resize(4);
  c.panes[0].base = "/a";
  GridConfig copy = c;
  resize_panes(c, 2, 2);
  EXPECT_EQ(c, copy);
}

// --- parse_image_topics ----------------------------------------------------

TEST(ConfigModel, ParseTopicsFfmpegSiblingWithNoBase)
{
  std::map<std::string, std::vector<std::string>> topics = {
    {"/cam/image_raw/ffmpeg", {"ffmpeg_image_transport_msgs/msg/FFMPEGPacket"}},
  };
  auto pairs = parse_image_topics(topics);
  ASSERT_EQ(pairs.size(), 1u);
  EXPECT_EQ(pairs[0].first, "/cam/image_raw");
  EXPECT_EQ(pairs[0].second, "ffmpeg");
}

TEST(ConfigModel, ParseTopicsCompressedSiblingWithBase)
{
  std::map<std::string, std::vector<std::string>> topics = {
    {"/cam/image_raw", {"sensor_msgs/msg/Image"}},
    {"/cam/image_raw/compressed", {"sensor_msgs/msg/CompressedImage"}},
  };
  auto pairs = parse_image_topics(topics);
  ASSERT_EQ(pairs.size(), 2u);
  // Sorted lexicographically by (base, transport), so "compressed" < "raw"
  // when the base is the same.
  EXPECT_EQ(pairs[0], (std::pair<std::string, std::string>{"/cam/image_raw", "compressed"}));
  EXPECT_EQ(pairs[1], (std::pair<std::string, std::string>{"/cam/image_raw", "raw"}));
}

TEST(ConfigModel, ParseTopicsRawImageByType)
{
  std::map<std::string, std::vector<std::string>> topics = {
    {"/some/image_raw", {"sensor_msgs/msg/Image"}},
  };
  auto pairs = parse_image_topics(topics);
  ASSERT_EQ(pairs.size(), 1u);
  EXPECT_EQ(pairs[0].first, "/some/image_raw");
  EXPECT_EQ(pairs[0].second, "raw");
}

TEST(ConfigModel, ParseTopicsFiltersUnknownTypes)
{
  std::map<std::string, std::vector<std::string>> topics = {
    {"/scan", {"sensor_msgs/msg/LaserScan"}},
    {"/tf", {"tf2_msgs/msg/TFMessage"}},
  };
  auto pairs = parse_image_topics(topics);
  EXPECT_TRUE(pairs.empty());
}

TEST(ConfigModel, ParseTopicsMixedNamespace)
{
  std::map<std::string, std::vector<std::string>> topics = {
    {"/bizzy/oak_forward/image_raw/ffmpeg", {"ffmpeg_image_transport_msgs/msg/FFMPEGPacket"}},
    {"/bizzy/oak_forward/segmentation/compressed", {"sensor_msgs/msg/CompressedImage"}},
    {"/bizzy/oak_forward/segmentation/camera_info", {"sensor_msgs/msg/CameraInfo"}},
  };
  auto pairs = parse_image_topics(topics);
  ASSERT_EQ(pairs.size(), 2u);
  EXPECT_EQ(pairs[0].first, "/bizzy/oak_forward/image_raw");
  EXPECT_EQ(pairs[0].second, "ffmpeg");
  EXPECT_EQ(pairs[1].first, "/bizzy/oak_forward/segmentation");
  EXPECT_EQ(pairs[1].second, "compressed");
}
