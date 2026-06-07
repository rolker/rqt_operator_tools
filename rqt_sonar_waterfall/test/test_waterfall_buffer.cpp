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

#include "rqt_sonar_waterfall/waterfall_buffer.hpp"

namespace
{

using rqt_sonar_waterfall::WaterfallBuffer;
using rqt_sonar_waterfall::WaterfallRow;

// A row tagged by stamp so eviction order is observable.
WaterfallRow tagged(double stamp)
{
  WaterfallRow r;
  r.stamp = stamp;
  return r;
}

}  // namespace

TEST(WaterfallBuffer, PushEvictsOldestBeyondCapacity)
{
  WaterfallBuffer buf(3);
  buf.push(tagged(1));
  buf.push(tagged(2));
  buf.push(tagged(3));
  buf.push(tagged(4));  // evicts stamp 1
  ASSERT_EQ(buf.size(), 3u);
  EXPECT_DOUBLE_EQ(buf.rows().front().stamp, 2.0);  // oldest retained
  EXPECT_DOUBLE_EQ(buf.rows().back().stamp, 4.0);   // newest
}

TEST(WaterfallBuffer, SetCapacityShrinkEvictsOldest)
{
  WaterfallBuffer buf(5);
  for (int i = 1; i <= 5; ++i) {
    buf.push(tagged(i));
  }
  buf.set_capacity(2);
  ASSERT_EQ(buf.size(), 2u);
  EXPECT_DOUBLE_EQ(buf.rows().front().stamp, 4.0);
  EXPECT_DOUBLE_EQ(buf.rows().back().stamp, 5.0);
}

TEST(WaterfallBuffer, CapacityClampedToAtLeastOne)
{
  WaterfallBuffer buf(0);
  EXPECT_EQ(buf.capacity(), 1u);
  buf.push(tagged(1));
  buf.push(tagged(2));
  EXPECT_EQ(buf.size(), 1u);
  EXPECT_DOUBLE_EQ(buf.rows().back().stamp, 2.0);
}

TEST(WaterfallBuffer, ClearEmpties)
{
  WaterfallBuffer buf(4);
  buf.push(tagged(1));
  buf.push(tagged(2));
  buf.clear();
  EXPECT_TRUE(buf.empty());
  EXPECT_EQ(buf.size(), 0u);
}
