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

#include "rqt_camera_grid/config_model.hpp"

#include <yaml-cpp/yaml.h>  // NOLINT(build/include_order): yaml.h is cpplint-classified as C system

#include <algorithm>
#include <array>
#include <fstream>
#include <sstream>

namespace rqt_camera_grid
{

namespace
{

const std::array<std::string, 4> kKnownTransportSuffixes{
  "/ffmpeg", "/compressed", "/compressedDepth", "/theora"};

std::string suffix_to_transport(const std::string & suffix)
{
  // Strip leading slash.
  return suffix.empty() ? std::string() : suffix.substr(1);
}

bool ends_with(const std::string & s, const std::string & suf)
{
  return s.size() >= suf.size() &&
         s.compare(s.size() - suf.size(), suf.size(), suf) == 0;
}

bool has_type(const std::vector<std::string> & types, const std::string & needle)
{
  return std::find(types.begin(), types.end(), needle) != types.end();
}

}  // namespace

std::string config_to_yaml(const GridConfig & config)
{
  YAML::Emitter out;
  out << YAML::BeginMap;
  out << YAML::Key << "grid" << YAML::Value << YAML::BeginMap
      << YAML::Key << "rows" << YAML::Value << config.rows
      << YAML::Key << "cols" << YAML::Value << config.cols
      << YAML::EndMap;
  out << YAML::Key << "panes" << YAML::Value << YAML::BeginSeq;
  for (const auto & p : config.panes) {
    out << YAML::Flow << YAML::BeginMap
        << YAML::Key << "base" << YAML::Value << p.base
        << YAML::Key << "transport" << YAML::Value << p.transport
        << YAML::Key << "warn_s" << YAML::Value << p.warn_s
        << YAML::Key << "error_s" << YAML::Value << p.error_s
        << YAML::EndMap;
  }
  out << YAML::EndSeq;
  out << YAML::EndMap;
  return out.c_str();
}

GridConfig config_from_yaml(const std::string & text)
{
  YAML::Node root;
  try {
    root = YAML::Load(text);
  } catch (const YAML::Exception & e) {
    throw ConfigParseError(std::string("malformed YAML: ") + e.what());
  }
  if (!root.IsMap()) {
    throw ConfigParseError("top-level YAML must be a map");
  }
  if (!root["grid"] || !root["grid"].IsMap()) {
    throw ConfigParseError("missing or non-map 'grid' key");
  }
  const auto & grid_node = root["grid"];
  if (!grid_node["rows"] || !grid_node["cols"]) {
    throw ConfigParseError("'grid' must contain 'rows' and 'cols'");
  }
  GridConfig out;
  try {
    out.rows = grid_node["rows"].as<int>();
    out.cols = grid_node["cols"].as<int>();
  } catch (const YAML::Exception & e) {
    throw ConfigParseError(std::string("'grid.rows'/'grid.cols' must be int: ") + e.what());
  }
  if (out.rows < 1 || out.cols < 1) {
    throw ConfigParseError("'grid.rows' and 'grid.cols' must be >= 1");
  }

  if (!root["panes"]) {
    return out;  // empty panes list is fine; cells render as placeholders.
  }
  if (!root["panes"].IsSequence()) {
    throw ConfigParseError("'panes' must be a sequence");
  }

  for (size_t i = 0; i < root["panes"].size(); ++i) {
    const auto & pnode = root["panes"][i];
    if (!pnode.IsMap()) {
      throw ConfigParseError("'panes[" + std::to_string(i) + "]' must be a map");
    }
    PaneConfig p;
    if (!pnode["base"]) {
      throw ConfigParseError("'panes[" + std::to_string(i) + "]' missing 'base'");
    }
    try {
      p.base = pnode["base"].as<std::string>();
    } catch (const YAML::Exception &) {
      throw ConfigParseError("'panes[" + std::to_string(i) + "].base' must be a string");
    }
    if (pnode["transport"]) {
      try {
        p.transport = pnode["transport"].as<std::string>();
      } catch (const YAML::Exception &) {
        throw ConfigParseError(
          "'panes[" + std::to_string(i) + "].transport' must be a string");
      }
    }
    static const std::array<std::string, 5> kValidTransports{
      "raw", "compressed", "compressedDepth", "theora", "ffmpeg"};
    if (std::find(kValidTransports.begin(), kValidTransports.end(), p.transport) ==
      kValidTransports.end())
    {
      throw ConfigParseError(
        "'panes[" + std::to_string(i) + "].transport' unknown transport '" +
        p.transport + "'");
    }
    if (pnode["warn_s"]) {
      try {
        p.warn_s = pnode["warn_s"].as<double>();
      } catch (const YAML::Exception &) {
        throw ConfigParseError(
          "'panes[" + std::to_string(i) + "].warn_s' must be a number");
      }
      if (p.warn_s < 0.0) {
        throw ConfigParseError(
          "'panes[" + std::to_string(i) + "].warn_s' must be >= 0");
      }
    }
    if (pnode["error_s"]) {
      try {
        p.error_s = pnode["error_s"].as<double>();
      } catch (const YAML::Exception &) {
        throw ConfigParseError(
          "'panes[" + std::to_string(i) + "].error_s' must be a number");
      }
      if (p.error_s < 0.0) {
        throw ConfigParseError(
          "'panes[" + std::to_string(i) + "].error_s' must be >= 0");
      }
    }
    if (p.error_s < p.warn_s) {
      throw ConfigParseError(
        "'panes[" + std::to_string(i) + "]' requires error_s >= warn_s");
    }
    out.panes.push_back(p);
  }
  return out;
}

void config_to_file(const GridConfig & config, const std::string & path)
{
  std::ofstream os(path);
  if (!os) {
    throw ConfigParseError("cannot open '" + path + "' for writing");
  }
  os << config_to_yaml(config);
  if (!os) {
    throw ConfigParseError("failed writing to '" + path + "'");
  }
}

GridConfig config_from_file(const std::string & path)
{
  std::ifstream is(path);
  if (!is) {
    throw ConfigParseError("cannot open '" + path + "' for reading");
  }
  std::stringstream ss;
  ss << is.rdbuf();
  return config_from_yaml(ss.str());
}

void resize_panes(GridConfig & config, int new_rows, int new_cols)
{
  if (new_rows < 1) {new_rows = 1;}
  if (new_cols < 1) {new_cols = 1;}
  config.rows = new_rows;
  config.cols = new_cols;
  const size_t target = static_cast<size_t>(new_rows) * new_cols;
  if (config.panes.size() > target) {
    config.panes.resize(target);
  } else {
    while (config.panes.size() < target) {
      config.panes.push_back(PaneConfig{});
    }
  }
}

std::vector<std::pair<std::string, std::string>> parse_image_topics(
  const std::map<std::string, std::vector<std::string>> & topic_types)
{
  std::vector<std::pair<std::string, std::string>> out;
  for (const auto & [topic, types] : topic_types) {
    // Known transport suffixes first.
    bool matched = false;
    for (const auto & suf : kKnownTransportSuffixes) {
      if (ends_with(topic, suf)) {
        const std::string base = topic.substr(0, topic.size() - suf.size());
        out.emplace_back(base, suffix_to_transport(suf));
        matched = true;
        break;
      }
    }
    if (matched) {continue;}

    // Otherwise, accept raw Image topics; filter out unrelated types.
    if (has_type(types, "sensor_msgs/msg/Image")) {
      out.emplace_back(topic, "raw");
    }
  }
  std::sort(out.begin(), out.end());
  return out;
}

}  // namespace rqt_camera_grid
