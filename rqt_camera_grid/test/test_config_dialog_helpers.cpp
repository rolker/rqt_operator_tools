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

// Pins the contract of detail::resolve_base_from_combo against
// regression. The function is the smallest unit of the #27 SIGABRT fix
// (b449acb) — it decides whether to commit the combo's display label
// or the clean topic from itemData into PaneConfig::base.

#include <gtest/gtest.h>

#include <QString>

#include "rqt_camera_grid/detail/config_dialog_helpers.hpp"

using rqt_camera_grid::detail::resolve_base_from_combo;

namespace
{

// Mirrors the populate_topic_combo() formatting: "<base>  [<transport>]".
const QString kBracketLabel = "/bizzy/sensors/cameras/oak_port/segmentation  [raw]";
const QString kCleanBase = "/bizzy/sensors/cameras/oak_port/segmentation";

}  // namespace

TEST(ResolveBaseFromCombo, NegativeIndexReturnsCurrentText)
{
  // No item selected — currentText is whatever the user typed. There is
  // no item to read itemData from; trust the line edit.
  EXPECT_EQ(
    resolve_base_from_combo(-1, "/some/topic", QString(), QString()),
    "/some/topic");
}

TEST(ResolveBaseFromCombo, IndexZeroReturnsCurrentText)
{
  // Index 0 is the "leave-empty" sentinel item; the > 0 guard means we
  // never read its itemData. Currently both paths return empty for the
  // sentinel, but a free-typed entry while idx is still 0 must round-
  // trip the typed text, not collapse to the sentinel's empty data.
  EXPECT_EQ(
    resolve_base_from_combo(0, "", "", ""),
    "");
  EXPECT_EQ(
    resolve_base_from_combo(0, "/freshly/typed", "", ""),
    "/freshly/typed");
}

TEST(ResolveBaseFromCombo, MatchingItemUsesItemData)
{
  // The exact #27 path: combo currentIndex points at a populated item,
  // currentText is the bracketed display label (dirty), itemData is the
  // clean topic. We must commit the clean topic.
  EXPECT_EQ(
    resolve_base_from_combo(2, kBracketLabel, kBracketLabel, kCleanBase),
    kCleanBase.toStdString());
}

TEST(ResolveBaseFromCombo, FreeTypedFallsBackToCurrentText)
{
  // User selected an item, then edited the line edit to a custom topic.
  // currentIndex still points at the old item, but currentText no
  // longer matches that item's display label. Trust the line edit.
  EXPECT_EQ(
    resolve_base_from_combo(2, "/custom/topic", kBracketLabel, kCleanBase),
    "/custom/topic");
}

TEST(ResolveBaseFromCombo, TextAlreadyCleanedFallsBackToCurrentText)
{
  // After onBaseEditChanged → setEditText(clean) has run, currentText
  // is already the clean topic and no longer matches the bracketed
  // display label. Trust the line edit (which is now clean) — round-
  // tripping idempotently.
  EXPECT_EQ(
    resolve_base_from_combo(2, kCleanBase, kBracketLabel, kCleanBase),
    kCleanBase.toStdString());
}

TEST(ResolveBaseFromCombo, EmptyEverythingProducesEmptyString)
{
  // Pure defensive case: a combo with the leave-empty sentinel selected
  // and nothing typed. Empty in, empty out — confirms we never produce
  // a stray sentinel value.
  EXPECT_EQ(
    resolve_base_from_combo(0, "", "", ""),
    "");
}
