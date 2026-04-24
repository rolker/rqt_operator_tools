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

#include "rqt_camera_grid/grid_layout.hpp"

#include <algorithm>
#include <cmath>

namespace rqt_camera_grid
{

std::vector<CellGeometry> compute_grid_layout(
  int w, int h, int rows, int cols, double target_aspect)
{
  std::vector<CellGeometry> out;
  if (rows <= 0 || cols <= 0 || w <= 0 || h <= 0 || !std::isfinite(target_aspect) ||
    target_aspect <= 0.0)
  {
    return out;
  }

  // Pick tight cell size preserving target_aspect = cell_w / cell_h.
  // Try height-limited first, then width-limited.
  double cell_h_d = static_cast<double>(h) / rows;
  double cell_w_d = cell_h_d * target_aspect;
  if (cell_w_d * cols > w) {
    // Width-limited.
    cell_w_d = static_cast<double>(w) / cols;
    cell_h_d = cell_w_d / target_aspect;
  }

  int cell_w = static_cast<int>(std::floor(cell_w_d));
  int cell_h = static_cast<int>(std::floor(cell_h_d));
  if (cell_w <= 0 || cell_h <= 0) {
    return out;
  }

  int total_w = cell_w * cols;
  int total_h = cell_h * rows;
  int slack_x = std::max(0, w - total_w);
  int slack_y = std::max(0, h - total_h);
  int pad_left = slack_x / 2;
  int pad_right = slack_x - pad_left;
  int pad_top = slack_y / 2;
  int pad_bottom = slack_y - pad_top;

  out.reserve(static_cast<size_t>(rows) * cols);
  for (int r = 0; r < rows; ++r) {
    for (int c = 0; c < cols; ++c) {
      int image_x = pad_left + c * cell_w;
      int image_y = pad_top + r * cell_h;
      Rect image_rect{image_x, image_y, cell_w, cell_h};

      // Widget (cell) rect absorbs the outer pad on edges that touch the
      // widget boundary. Interior edges carry no padding.
      int cell_x = image_x;
      int cell_y = image_y;
      int cell_cw = cell_w;
      int cell_ch = cell_h;
      if (c == 0) {
        cell_x -= pad_left;
        cell_cw += pad_left;
      }
      if (c == cols - 1) {
        cell_cw += pad_right;
      }
      if (r == 0) {
        cell_y -= pad_top;
        cell_ch += pad_top;
      }
      if (r == rows - 1) {
        cell_ch += pad_bottom;
      }
      Rect cell_rect{cell_x, cell_y, cell_cw, cell_ch};

      out.push_back(CellGeometry{r, c, cell_rect, image_rect});
    }
  }
  return out;
}

}  // namespace rqt_camera_grid
