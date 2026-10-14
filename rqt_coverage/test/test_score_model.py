"""Score maths and message sanitising: no Qt, no ROS, no display."""

import math

import pytest

from fake_msgs import assessment, source
from rqt_coverage.score_model import (
    AreaScore,
    combined_score,
    fraction,
    pool,
)


class TestRatios:
    def test_basic(self):
        s = AreaScore.from_msg(assessment(total=1000, covered=600, meeting=300))
        assert s.pct_covered == pytest.approx(0.6)
        assert s.pct_to_spec == pytest.approx(0.3)
        assert not s.clamped

    def test_zero_total_is_none_not_a_crash(self):
        s = AreaScore.from_msg(assessment(total=0, covered=0, meeting=0))
        assert s.pct_covered is None
        assert s.pct_to_spec is None

    def test_fraction_guards(self):
        assert fraction(1.0, 0.0) is None
        assert fraction(1.0, float('nan')) is None
        assert fraction(1.0, -5.0) is None

    def test_title_is_the_area_id(self):
        assert AreaScore.from_msg(assessment(area_id='lake-1')).title == 'lake-1'
        assert AreaScore.from_msg(assessment(area_id='')).title == '(no id)'


class TestSanitising:
    def test_meeting_above_covered_is_clamped_and_flagged(self):
        s = AreaScore.from_msg(assessment(total=100, covered=40, meeting=70))
        assert s.meeting == 40
        assert s.clamped

    def test_covered_above_total_is_clamped_and_flagged(self):
        s = AreaScore.from_msg(assessment(total=100, covered=150, meeting=50))
        assert s.covered == 100
        assert s.clamped

    @pytest.mark.parametrize('bad', [float('nan'), float('inf'), -3.0, None, 'x'])
    def test_bad_values_become_zero_and_flagged(self, bad):
        s = AreaScore.from_msg(assessment(total=100, covered=bad, meeting=0))
        assert s.covered == 0.0
        assert s.clamped
        assert math.isfinite(s.pct_covered)

    def test_source_clamped_to_assessment_covered_and_to_its_own_covered(self):
        msg = assessment(total=100, covered=80, meeting=20,
                         sources=[source('b1', covered=500, meeting=900, unique=700)])
        s = AreaScore.from_msg(msg)
        src = s.sources[0]
        assert (src.covered, src.meeting, src.unique) == (80, 80, 80)
        assert s.clamped

    def test_source_covering_more_than_the_union_is_flagged(self):
        # Contract: a source's covered area is at most the assessment's.
        msg = assessment(total=100, covered=50, meeting=10,
                         sources=[source('b1', covered=60, meeting=5, unique=0)])
        s = AreaScore.from_msg(msg)
        assert s.sources[0].covered == 50
        assert s.clamped

    def test_unique_credit_summing_above_covered_is_flagged(self):
        # Contract: the sum of the sources' unique areas is at most covered.
        msg = assessment(total=200, covered=100, meeting=0,
                         sources=[source('b1', 90, 0, 80), source('b2', 90, 0, 80)])
        s = AreaScore.from_msg(msg)
        assert s.clamped
        assert [x.unique for x in s.sources] == [80, 80]   # shown as sent

    def test_source_meeting_above_the_assessments_is_legal(self):
        # The union takes depth from the smallest uncertainty, so one source can
        # meet the standard over more cells than the assessment does.
        msg = assessment(total=100, covered=80, meeting=10,
                         sources=[source('b1', 70, 40, 0)])
        assert not AreaScore.from_msg(msg).clamped

    def test_clean_message_is_not_flagged(self):
        msg = assessment(total=100, covered=80, meeting=20,
                         sources=[source('b1', 80, 20, 10)])
        assert not AreaScore.from_msg(msg).clamped


class TestCells:
    def test_counts(self):
        s = AreaScore.from_msg(assessment(total=400, covered=200, meeting=100,
                                          cell_size=2.0))
        assert s.cells == (100, 50, 25)

    @pytest.mark.parametrize('cs', [1e-160, 1e-200, 5e-324, 1e160])
    def test_extreme_cell_sizes_do_not_raise(self, cs):
        # cs*cs underflows to 0 or overflows to inf; the counts are unavailable.
        s = AreaScore.from_msg(assessment(total=400, covered=200, meeting=100,
                                          cell_size=cs))
        assert s.cells is None or all(isinstance(n, int) for n in s.cells)

    @pytest.mark.parametrize('cs', [0.0, -1.0, float('nan')])
    def test_no_usable_cell_size(self, cs):
        assert AreaScore.from_msg(assessment(cell_size=cs)).cells is None


class TestPool:
    def test_big_area_weighs_more(self):
        big = AreaScore.from_msg(assessment('big', 9000, 9000, 9000))
        small = AreaScore.from_msg(assessment('small', 1000, 0, 0))
        p = pool([big, small])
        assert p.pct_covered == pytest.approx(0.9)   # not the 0.5 mean of 1.0 and 0.0
        assert p.pct_to_spec == pytest.approx(0.9)
        assert p.count == 2

    def test_zero_total_areas_are_skipped(self):
        empty = AreaScore.from_msg(assessment('e', 0, 0, 0))
        a = AreaScore.from_msg(assessment('a', 100, 50, 25))
        p = pool([empty, a])
        assert p.count == 1
        assert p.pct_covered == pytest.approx(0.5)

    def test_no_areas(self):
        p = pool([])
        assert p.pct_covered is None and p.count == 0


class TestCombined:
    def test_default_weights(self):
        # 30 % to spec counts fully, the other 30 % covered counts half.
        assert combined_score(0.6, 0.3) == pytest.approx(45.0)

    def test_custom_weights(self):
        assert combined_score(0.6, 0.3, 1.0, 0.0) == pytest.approx(30.0)
        assert combined_score(0.6, 0.3, 0.8, 0.25) == pytest.approx(31.5)

    def test_invalid_weights_fall_back_to_defaults(self):
        assert combined_score(0.6, 0.3, float('nan'), 7.0) == pytest.approx(45.0)

    def test_missing_ratio_is_none(self):
        assert combined_score(None, 0.3) is None
        assert combined_score(0.6, None) is None

    def test_full_marks(self):
        assert combined_score(1.0, 1.0) == pytest.approx(100.0)
