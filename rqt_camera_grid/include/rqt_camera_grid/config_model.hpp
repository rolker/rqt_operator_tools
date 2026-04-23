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

#ifndef RQT_CAMERA_GRID__CONFIG_MODEL_HPP_
#define RQT_CAMERA_GRID__CONFIG_MODEL_HPP_

#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace rqt_camera_grid
{

inline constexpr double kDefaultWarnS = 2.0;
inline constexpr double kDefaultErrorS = 5.0;

struct PaneConfig
{
  std::string base;
  std::string transport{"raw"};
  double warn_s{kDefaultWarnS};
  double error_s{kDefaultErrorS};

  bool operator==(const PaneConfig & other) const
  {
    return base == other.base && transport == other.transport &&
           warn_s == other.warn_s && error_s == other.error_s;
  }
};

struct GridConfig
{
  int rows{2};
  int cols{2};
  std::vector<PaneConfig> panes;

  bool operator==(const GridConfig & other) const
  {
    return rows == other.rows && cols == other.cols && panes == other.panes;
  }
};

class ConfigParseError : public std::runtime_error
{
public:
  using std::runtime_error::runtime_error;
};

// Serialize config to YAML. Output is suitable for persistence (rqt
// perspective string) and for human hand-editing.
std::string config_to_yaml(const GridConfig & config);

// Parse a YAML string into a GridConfig. Throws ConfigParseError with a
// descriptive message on any failure (malformed YAML, missing or wrong-typed
// fields, out-of-range values).
GridConfig config_from_yaml(const std::string & text);

// File-backed helpers. Also throw ConfigParseError for I/O errors.
void config_to_file(const GridConfig & config, const std::string & path);
GridConfig config_from_file(const std::string & path);

// Resize config.panes to exactly rows*cols entries:
// - shrinks by truncating trailing panes
// - grows by appending default-constructed PaneConfig{} entries
// Also updates config.rows/cols. Used by the config dialog's row/col
// spinbox handlers and at load time when len(panes) disagrees with
// rows*cols (caller is expected to log a warning in the load path).
void resize_panes(GridConfig & config, int new_rows, int new_cols);

// Parse the output shape of rclcpp::Node::get_topic_names_and_types()
// (map<topic_name, vector<type_name>>) into a list of image-shaped (base,
// transport) pairs. Recognized transports: ffmpeg, compressed,
// compressedDepth, theora. Topics whose name doesn't end in a known
// transport suffix are treated as raw iff their type is
// sensor_msgs/msg/Image. Unknown types are filtered out.
std::vector<std::pair<std::string, std::string>> parse_image_topics(
  const std::map<std::string, std::vector<std::string>> & topic_types);

}  // namespace rqt_camera_grid

#endif  // RQT_CAMERA_GRID__CONFIG_MODEL_HPP_
