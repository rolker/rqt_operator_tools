"""Regression tests for the size-hint invariants from issue #19.

The annunciator must fill any space its parent gives it but never push the
parent wider than intended.  Concretely:

- ``IndicatorWidget.minimumSizeHint()`` is a small constant — it does not
  grow with font size, text length, or the ``update_font_size`` calls made
  during reflow.
- The child labels use ``QSizePolicy.Ignored`` horizontally so their
  font-scaled minimums do not bubble up through parent layouts.
- ``AnnunciatorWidget`` uses ``QLayout.SetNoConstraint`` so the grid's
  aggregated minimum does not become this widget's minimum, and its own
  ``minimumSizeHint()`` is likewise a small constant.
"""

import os

import pytest

os.environ.setdefault('QT_QPA_PLATFORM', 'offscreen')

from python_qt_binding.QtWidgets import QApplication, QLayout, QSizePolicy

from rqt_annunciator.config_model import AnnunciatorConfig, IndicatorConfig
from rqt_annunciator.indicator_widget import IndicatorWidget


@pytest.fixture(scope='module')
def qapp():
    app = QApplication.instance() or QApplication([])
    yield app


class _FakeLogger:
    def warn(self, *_args, **_kwargs):
        pass

    def error(self, *_args, **_kwargs):
        pass


class _FakeNode:
    def get_logger(self):
        return _FakeLogger()

    def create_subscription(self, *_args, **_kwargs):
        return object()

    def destroy_subscription(self, *_args, **_kwargs):
        pass


class TestIndicatorWidgetSizeInvariants:
    def test_minimum_size_hint_is_fixed(self, qapp):
        from rqt_annunciator.config_model import IndicatorLevel
        w = IndicatorWidget('demo')
        before = w.minimumSizeHint()
        # Force a large cell so per-cell font fitting picks a huge font —
        # this used to drive the widget's minimum upward through
        # QLabel.minimumSizeHint(), widening the rqt window on vertical
        # drags.
        w.resize(800, 400)
        w.set_status(IndicatorLevel.OK,
                     'a very long value text that would be huge')
        after = w.minimumSizeHint()
        assert after == before, (
            'IndicatorWidget.minimumSizeHint() must not grow with font size '
            'or text length (issue #19)'
        )
        assert before.width() <= 64 and before.height() <= 32, (
            'minimum must stay small so rqt can shrink the window freely'
        )

    def test_labels_have_ignored_horizontal_policy(self, qapp):
        w = IndicatorWidget('demo')
        for child_label in (w._label, w._value_label):
            assert child_label.sizePolicy().horizontalPolicy() == QSizePolicy.Ignored, (
                'Child labels must use Ignored horizontal policy so their '
                'font-scaled minimums do not bubble up (issue #19)'
            )

    def test_widget_policy_is_expanding(self, qapp):
        w = IndicatorWidget('demo')
        assert w.sizePolicy().horizontalPolicy() == QSizePolicy.Expanding
        assert w.sizePolicy().verticalPolicy() == QSizePolicy.Expanding


class TestAnnunciatorWidgetSizeInvariants:
    def _make_widget(self, n_indicators=6):
        # Import here so the module-level QT_QPA_PLATFORM default is applied
        # before any Qt setup in annunciator_widget.
        from rqt_annunciator.annunciator_widget import AnnunciatorWidget

        w = AnnunciatorWidget(_FakeNode())
        config = AnnunciatorConfig(
            indicators=[
                IndicatorConfig(name=f'ind_{i}', source='diagnostics',
                                diagnostic_name=f'd_{i}')
                for i in range(n_indicators)
            ]
        )
        w.load_config(config)
        return w

    def test_layout_size_constraint_is_none(self, qapp):
        w = self._make_widget()
        assert w._layout.sizeConstraint() == QLayout.SetNoConstraint, (
            'Without SetNoConstraint, the grid forces its aggregated minimum '
            'onto the widget and up to the rqt window (issue #19)'
        )

    def test_minimum_size_hint_does_not_grow_with_indicator_count(self, qapp):
        few = self._make_widget(n_indicators=2)
        many = self._make_widget(n_indicators=24)
        assert few.minimumSizeHint() == many.minimumSizeHint(), (
            'AnnunciatorWidget.minimumSizeHint() must be constant regardless '
            'of indicator count, or the rqt window grows with config size'
        )

    def test_minimum_size_hint_survives_font_scaling(self, qapp):
        w = self._make_widget(n_indicators=6)
        before = w.minimumSizeHint()
        # Simulate a tall window triggering huge fonts via _rebuild_layout.
        w.resize(400, 2000)
        w._rebuild_layout()
        after = w.minimumSizeHint()
        assert after == before, (
            'Font rescaling on resize must not feed back into the widget\'s '
            'minimumSizeHint (issue #19)'
        )
