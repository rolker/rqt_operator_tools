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

#include "rqt_camera_grid/grid_layout.hpp"

using rqt_camera_grid::compute_grid_layout;

TEST(GridLayout, EmptyOnInvalidInputs)
{
  EXPECT_TRUE(compute_grid_layout(0, 100, 2, 2, 16.0 / 9.0).empty());
  EXPECT_TRUE(compute_grid_layout(100, 0, 2, 2, 16.0 / 9.0).empty());
  EXPECT_TRUE(compute_grid_layout(100, 100, 0, 2, 16.0 / 9.0).empty());
  EXPECT_TRUE(compute_grid_layout(100, 100, 2, 0, 16.0 / 9.0).empty());
  EXPECT_TRUE(compute_grid_layout(100, 100, 2, 2, 0.0).empty());
  EXPECT_TRUE(compute_grid_layout(100, 100, 2, 2, -1.0).empty());
}

TEST(GridLayout, PerfectFit2x2_16x9_in_1920x800)
{
  auto cells = compute_grid_layout(1920, 800, 2, 2, 16.0 / 9.0);
  ASSERT_EQ(cells.size(), 4u);

  // Height-limited: cell_h = 400, cell_w = 711 (floor of 711.11).
  for (const auto & c : cells) {
    EXPECT_EQ(c.image.w, 711);
    EXPECT_EQ(c.image.h, 400);
  }

  // Horizontal slack = 1920 - 2*711 = 498; split 249/249.
  const auto & top_left = cells[0];
  const auto & top_right = cells[1];
  const auto & bot_left = cells[2];
  const auto & bot_right = cells[3];

  EXPECT_EQ(top_left.image.x, 249);
  EXPECT_EQ(top_left.image.y, 0);
  EXPECT_EQ(top_right.image.x, 249 + 711);
  EXPECT_EQ(top_right.image.y, 0);
  EXPECT_EQ(bot_left.image.x, 249);
  EXPECT_EQ(bot_left.image.y, 400);
  EXPECT_EQ(bot_right.image.x, 249 + 711);
  EXPECT_EQ(bot_right.image.y, 400);

  // Inner vertical boundary at x=960: right edge of left column == left edge of right column.
  EXPECT_EQ(top_left.image.x + top_left.image.w, top_right.image.x);
  EXPECT_EQ(bot_left.image.x + bot_left.image.w, bot_right.image.x);

  // Inner horizontal boundary at y=400: bottom of top row == top of bottom row.
  EXPECT_EQ(top_left.image.y + top_left.image.h, bot_left.image.y);
}

TEST(GridLayout, NoGapBetweenInnerImages_GenericWxH)
{
  // Any sensible input: inner boundaries must be shared. No horizontal or
  // vertical gap between neighboring image rects.
  for (int w : {400, 1000, 1920, 3840}) {
    for (int h : {300, 600, 1080, 2160}) {
      for (int rows : {1, 2, 3}) {
        for (int cols : {1, 2, 3, 4}) {
          auto cells = compute_grid_layout(w, h, rows, cols, 16.0 / 9.0);
          if (cells.empty()) {continue;}
          // Compare right-of-cell == left-of-next-in-row, bottom-of-cell == top-of-next-in-col.
          for (int r = 0; r < rows; ++r) {
            for (int c = 0; c + 1 < cols; ++c) {
              const auto & a = cells[r * cols + c];
              const auto & b = cells[r * cols + (c + 1)];
              EXPECT_EQ(a.image.x + a.image.w, b.image.x)
                << "horizontal gap at W=" << w << " H=" << h
                << " rows=" << rows << " cols=" << cols
                << " r=" << r << " c=" << c;
            }
          }
          for (int r = 0; r + 1 < rows; ++r) {
            for (int c = 0; c < cols; ++c) {
              const auto & a = cells[r * cols + c];
              const auto & b = cells[(r + 1) * cols + c];
              EXPECT_EQ(a.image.y + a.image.h, b.image.y);
            }
          }
        }
      }
    }
  }
}

TEST(GridLayout, SlackGoesToOutsideEdgesOnly)
{
  // 1x2 grid, 16:9 target, 2000x300 widget (very wide) -> height-limited.
  auto cells = compute_grid_layout(2000, 300, 1, 2, 16.0 / 9.0);
  ASSERT_EQ(cells.size(), 2u);
  // cell_h=300, cell_w = floor(300*16/9) = 533. total_w=1066, slack_x=934.
  EXPECT_EQ(cells[0].image.w, 533);
  EXPECT_EQ(cells[0].image.x, 467);  // floor(934/2) = 467
  EXPECT_EQ(cells[1].image.x, 467 + 533);
  // Left cell widget absorbs left pad (pad_left = 467).
  EXPECT_EQ(cells[0].cell.x, 0);
  EXPECT_EQ(cells[0].cell.w, 533 + 467);
  // Right cell widget absorbs right pad (pad_right = 467).
  EXPECT_EQ(cells[1].cell.x, 467 + 533);
  EXPECT_EQ(cells[1].cell.w, 533 + 467);
  // Full widget width accounted for.
  EXPECT_EQ(cells[0].cell.w + cells[1].cell.w, 2000);
}

TEST(GridLayout, WidthLimitedBranch)
{
  // 2x2 grid, 16:9 target, 1000x2000 widget (very tall) -> width-limited.
  auto cells = compute_grid_layout(1000, 2000, 2, 2, 16.0 / 9.0);
  ASSERT_EQ(cells.size(), 4u);
  // cell_w = 500, cell_h = floor(500 * 9/16) = 281. total_h = 562, slack_y = 1438.
  EXPECT_EQ(cells[0].image.w, 500);
  EXPECT_EQ(cells[0].image.h, 281);
  EXPECT_EQ(cells[0].image.x, 0);
  EXPECT_EQ(cells[0].image.y, 719);  // floor(1438/2)
  // Top row cells absorb top pad.
  EXPECT_EQ(cells[0].cell.y, 0);
  EXPECT_EQ(cells[0].cell.h, 281 + 719);
}

TEST(GridLayout, SingleCellFillsEntireWidget)
{
  auto cells = compute_grid_layout(1920, 1080, 1, 1, 16.0 / 9.0);
  ASSERT_EQ(cells.size(), 1u);
  // 1920x1080 is exactly 16:9, so no slack; cell fills widget.
  EXPECT_EQ(cells[0].image.w, 1920);
  EXPECT_EQ(cells[0].image.h, 1080);
  EXPECT_EQ(cells[0].cell.w, 1920);
  EXPECT_EQ(cells[0].cell.h, 1080);
}

TEST(GridLayout, EdgeCellsCarryOuterPaddingOnly)
{
  // 2x2 with slack on both axes.
  auto cells = compute_grid_layout(1100, 500, 2, 2, 1.0);  // 1:1 cells
  ASSERT_EQ(cells.size(), 4u);
  // Height-limited: cell_h = 250, cell_w = 250. total=500x500, slack_x=600, slack_y=0.
  // pad_left = 300, pad_right = 300, pad_top=0, pad_bot=0.
  const auto & tl = cells[0];  // top-left
  const auto & tr = cells[1];  // top-right
  // TL cell absorbs left pad only.
  EXPECT_EQ(tl.cell.x, 0);
  EXPECT_EQ(tl.cell.w, 250 + 300);
  EXPECT_EQ(tl.cell.y, tl.image.y);  // no vertical pad
  EXPECT_EQ(tl.cell.h, tl.image.h);
  // TR cell absorbs right pad only.
  EXPECT_EQ(tr.cell.x, tr.image.x);
  EXPECT_EQ(tr.cell.w, 250 + 300);
  // Inner boundary is shared: TL right edge == TR left edge.
  EXPECT_EQ(tl.image.x + tl.image.w, tr.image.x);
}
