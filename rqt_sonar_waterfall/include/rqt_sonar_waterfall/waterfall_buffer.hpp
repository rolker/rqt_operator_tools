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

#ifndef RQT_SONAR_WATERFALL__WATERFALL_BUFFER_HPP_
#define RQT_SONAR_WATERFALL__WATERFALL_BUFFER_HPP_

#include <cstddef>
#include <deque>

#include "rqt_sonar_waterfall/waterfall_model.hpp"

namespace rqt_sonar_waterfall
{

/// Fixed-capacity history of waterfall rows (the scrollback).
///
/// Newest rows are pushed at the back; once capacity is exceeded the oldest
/// (front) rows are evicted. Rows may differ in width — the renderer maps each
/// to the viewport independently. Capacity is clamped to at least 1.
class WaterfallBuffer
{
public:
  static constexpr std::size_t kDefaultCapacity = 200;

  explicit WaterfallBuffer(std::size_t capacity = kDefaultCapacity);

  /// Change the scrollback depth, evicting oldest rows if shrinking.
  void set_capacity(std::size_t capacity);
  std::size_t capacity() const {return capacity_;}

  std::size_t size() const {return rows_.size();}
  bool empty() const {return rows_.empty();}
  void clear() {rows_.clear();}

  /// Append the newest row, evicting the oldest if at capacity.
  void push(WaterfallRow row);

  /// Stored rows, front = oldest, back = newest.
  const std::deque<WaterfallRow> & rows() const {return rows_;}

  /// Mutable access for in-place enrichment of buffered rows (e.g. populating
  /// the lazy TVG cache or re-deriving it after a slope change). Does not change
  /// row count or order.
  std::deque<WaterfallRow> & mutable_rows() {return rows_;}

private:
  void trim();

  std::size_t capacity_;
  std::deque<WaterfallRow> rows_;
};

}  // namespace rqt_sonar_waterfall

#endif  // RQT_SONAR_WATERFALL__WATERFALL_BUFFER_HPP_
