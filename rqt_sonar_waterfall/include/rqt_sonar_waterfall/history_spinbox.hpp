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

#ifndef RQT_SONAR_WATERFALL__HISTORY_SPINBOX_HPP_
#define RQT_SONAR_WATERFALL__HISTORY_SPINBOX_HPP_

#include <QSpinBox>

namespace rqt_sonar_waterfall
{

/// Configure a History spinbox with the shared parameters both sonar plugins
/// use, so the echogram and the waterfall present one consistent "how much you
/// see" knob: how many recent pings to retain and auto-fit across the canvas.
/// Range 1-5000 pings, coarse step of 50 (the default step of 1 is too fine to
/// cross that range by hand), and a shared tooltip. The buffer-size default is
/// per-plugin, so it is passed in rather than baked in.
inline void configure_history_spinbox(QSpinBox * spin, int default_value)
{
  spin->setRange(1, 5000);
  spin->setSingleStep(50);
  spin->setValue(default_value);
  spin->setToolTip(
    QObject::tr("History: number of recent pings to keep and auto-fit across the view."));
}

}  // namespace rqt_sonar_waterfall

#endif  // RQT_SONAR_WATERFALL__HISTORY_SPINBOX_HPP_
