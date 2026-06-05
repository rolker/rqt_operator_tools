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

#ifndef RQT_SONAR_WATERFALL__WATERFALL_WIDGET_HPP_
#define RQT_SONAR_WATERFALL__WATERFALL_WIDGET_HPP_

#include <QImage>
#include <QWidget>

namespace rqt_sonar_waterfall
{

/// Scrolling backscatter waterfall canvas.
///
/// Geometry-agnostic: it paints rows of intensities that have already been
/// decoded and assembled elsewhere (see the forthcoming WaterfallBuffer /
/// RowExtractor modules). This scaffold renders an empty dark canvas; the
/// row-rendering path is filled in by subsequent steps of issue #39.
class WaterfallWidget : public QWidget
{
  Q_OBJECT

public:
  explicit WaterfallWidget(QWidget * parent = nullptr);
  ~WaterfallWidget() override;

protected:
  void paintEvent(QPaintEvent * event) override;

private:
  // Backing image for the waterfall. Allocated lazily once a data path exists.
  QImage canvas_;
};

}  // namespace rqt_sonar_waterfall

#endif  // RQT_SONAR_WATERFALL__WATERFALL_WIDGET_HPP_
