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

#ifndef RQT_CAMERA_GRID__DETAIL__CONFIG_DIALOG_HELPERS_HPP_
#define RQT_CAMERA_GRID__DETAIL__CONFIG_DIALOG_HELPERS_HPP_

#include <QString>

#include <string>

namespace rqt_camera_grid::detail
{

// Decide what to write into PaneConfig::base when committing the editor.
//
// `base_combo_` items follow the pattern `(label="<base>  [<transport>]",
// data="<base>")` — the label is the user-friendly display, the data is
// the clean topic string. `lineEdit::editingFinished` can fire (combo
// popup-close focus shifts, autocomplete Enter/Tab) before the
// `currentIndexChanged → onBaseEditChanged → setEditText(clean)` chain
// has overwritten the line edit. In that window `currentText` is the
// dirty display label.
//
// Contract:
//   - If the line edit matches a known item's display text exactly
//     (i.e. the user picked an item, the chain hasn't run yet), use
//     that item's `itemData` — the clean base.
//   - Otherwise (free-typed custom topic, no match, or chain already
//     ran), trust the line edit.
//
// `current_index == 0` is the "leave-empty" sentinel item the combo
// adds first; both branches yield the empty string for it, but the
// guard makes that explicit.
inline std::string resolve_base_from_combo(
  int current_index,
  const QString & current_text,
  const QString & item_text_at_index,
  const QString & item_data_at_index)
{
  if (current_index > 0 && current_text == item_text_at_index) {
    return item_data_at_index.toStdString();
  }
  return current_text.toStdString();
}

}  // namespace rqt_camera_grid::detail

#endif  // RQT_CAMERA_GRID__DETAIL__CONFIG_DIALOG_HELPERS_HPP_
