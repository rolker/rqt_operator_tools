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

#ifndef RQT_SONAR_WATERFALL__PING_PAIRER_HPP_
#define RQT_SONAR_WATERFALL__PING_PAIRER_HPP_

#include <optional>

#include "rqt_sonar_waterfall/waterfall_model.hpp"

namespace rqt_sonar_waterfall
{

/// Decides when a port and/or starboard ping become one emitted waterfall row.
///
/// Port and starboard arrive on separate topics and are not perfectly
/// synchronized. The policy: when both sides are active, the starboard ping is
/// the trigger and is paired with the most recently seen port ping (one row per
/// starboard ping); a port ping only latches. When a single side is active, that
/// side triggers directly. This keeps the output rate at one row per ping-pair
/// rather than doubling it. State is not thread-safe — the caller serializes.
class PingPairer
{
public:
  /// Declare which sides are currently subscribed. Latched rows are cleared so
  /// stale data from a deselected side cannot leak into later output.
  void set_sides(bool port_active, bool starboard_active);

  bool port_active() const {return port_active_;}
  bool starboard_active() const {return starboard_active_;}

  /// Latch a port row; returns a row to emit only when port is the trigger
  /// (i.e. starboard is not active).
  std::optional<WaterfallRow> submit_port(WaterfallRow row);

  /// Latch a starboard row; returns the paired/combined row to emit when
  /// starboard is active.
  std::optional<WaterfallRow> submit_starboard(WaterfallRow row);

  /// Forget any latched rows without changing which sides are active.
  void reset();

private:
  bool port_active_ = false;
  bool starboard_active_ = false;
  std::optional<WaterfallRow> latest_port_;
  std::optional<WaterfallRow> latest_starboard_;
};

}  // namespace rqt_sonar_waterfall

#endif  // RQT_SONAR_WATERFALL__PING_PAIRER_HPP_
