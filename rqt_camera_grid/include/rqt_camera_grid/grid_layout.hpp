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

#ifndef RQT_CAMERA_GRID__GRID_LAYOUT_HPP_
#define RQT_CAMERA_GRID__GRID_LAYOUT_HPP_

#include <vector>

namespace rqt_camera_grid
{

struct Rect
{
  int x;
  int y;
  int w;
  int h;
};

struct CellGeometry
{
  int row;
  int col;
  Rect cell;   // full widget rect for the pane (may include outer padding)
  Rect image;  // image render rect (staleness border wraps this)
};

// Compute per-cell geometries for an R x C grid inside a WxH widget, with
// target cell aspect A (width / height). Slack from aspect mismatch goes to
// the outer edges of the grid only; inner boundaries are shared.
//
// Returned cells are ordered row-major (row=0, col=0..C-1; row=1, col=0..C-1; ...).
// Image rects are anchored toward the grid interior for edge cells; inner
// cells have image == cell. For mixed per-pane aspects, callers letterbox
// within their own image rect as a second step.
std::vector<CellGeometry> compute_grid_layout(
  int w, int h, int rows, int cols, double target_aspect);

}  // namespace rqt_camera_grid

#endif  // RQT_CAMERA_GRID__GRID_LAYOUT_HPP_
