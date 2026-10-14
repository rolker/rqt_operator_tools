"""Qt-free, ROS-free model for the coverage scoreboard.

Everything the unit tests exercise lives here so it runs without a display or
a ROS graph: turning a ``marine_interfaces/CoverageAssessment`` into a sanitised
``AreaScore``, the keep-latest-per-area ``Store``, the score maths (percent
covered, percent to standard, pooled roll-up, combined score), the aging
helper and the YAML-persisted ``CoverageConfig``.

The message comments are the contract.  In short: areas are m^2 of one
assessment raster, ``area_total`` can be 0, and the invariant is
``area_meeting_standard <= area_covered <= area_total``.  The scorer is a
prototype (unh_marine_autonomy#403), so this model reads the message
duck-typed and never assumes the invariant holds.
"""

import math
from dataclasses import dataclass, field, fields
from typing import Dict, Iterable, List, Optional, Tuple

import yaml

VIEW_GLANCE = 'glance'
VIEW_DETAIL = 'detail'
VIEWS = (VIEW_GLANCE, VIEW_DETAIL)

FONT_SCALE_MIN = 0.5
FONT_SCALE_MAX = 4.0

# Aging states of an area's last assessment.
FRESH = 'fresh'
AGING = 'aging'
NEVER = 'never'

# A message older (by header.stamp) than the held one is treated as a replay or
# an out-of-order delivery only when it arrives this soon after the held one.
# Later than that, a backwards stamp means the scorer's clock restarted (a sim
# restart), and the message is the newest information we have.
REPLAY_WINDOW_S = 2.0


def _clean_area(value) -> Tuple[float, bool]:
    """Return ``(m^2 value, changed)``; non-finite or negative becomes 0."""
    try:
        v = float(value)
    except (TypeError, ValueError):
        return 0.0, True
    if not math.isfinite(v) or v < 0.0:
        return 0.0, True
    return v, False


def _clamp_chain(total: float, covered: float, meeting: float):
    """Enforce ``meeting <= covered <= total``; return values and a changed flag."""
    new_covered = min(covered, total)
    new_meeting = min(meeting, new_covered)
    changed = (new_covered != covered) or (new_meeting != meeting)
    return new_covered, new_meeting, changed


def fraction(part: float, total: float) -> Optional[float]:
    """``part / total``, or ``None`` when the total is not positive and finite."""
    if not (math.isfinite(total) and total > 0.0):
        return None
    return part / total


@dataclass(frozen=True)
class SourceScore:
    """One source's (boat's) credit inside one area, in m^2."""

    source: str
    covered: float
    meeting: float
    unique: float


@dataclass(frozen=True)
class AreaScore:
    """A sanitised snapshot of one ``CoverageAssessment`` message."""

    area_id: str
    standard_name: str
    standard_a: float
    standard_b: float
    cell_size: float
    total: float
    covered: float
    meeting: float
    stamp_ns: int
    sources: Tuple[SourceScore, ...] = ()
    # True when sanitising changed a value: the scorer broke the message's
    # invariant, which an operator should not read as a clean number.
    clamped: bool = False

    @property
    def title(self) -> str:
        """Display name.  ``SurveyArea`` has no name field, only its id."""
        return self.area_id or '(no id)'

    @property
    def pct_covered(self) -> Optional[float]:
        """Covered fraction of the area, 0..1, or ``None`` for an empty area."""
        return fraction(self.covered, self.total)

    @property
    def pct_to_spec(self) -> Optional[float]:
        """Fraction of the whole area that meets the standard, 0..1 or ``None``."""
        return fraction(self.meeting, self.total)

    @property
    def cells(self) -> Optional[Tuple[int, int, int]]:
        """``(total, covered, meeting)`` cell counts, or ``None`` without a cell size."""
        if not (math.isfinite(self.cell_size) and self.cell_size > 0.0):
            return None
        cell_area = self.cell_size * self.cell_size
        return (round(self.total / cell_area),
                round(self.covered / cell_area),
                round(self.meeting / cell_area))

    @classmethod
    def from_msg(cls, msg) -> 'AreaScore':
        """Build from a ``CoverageAssessment`` (or any object shaped like one)."""
        changed = False
        total, bad = _clean_area(msg.area_total)
        changed |= bad
        covered, bad = _clean_area(msg.area_covered)
        changed |= bad
        meeting, bad = _clean_area(msg.area_meeting_standard)
        changed |= bad
        covered, meeting, bad = _clamp_chain(total, covered, meeting)
        changed |= bad

        sources: List[SourceScore] = []
        for src in msg.sources:
            s_cov, bad = _clean_area(src.area_covered)
            changed |= bad
            s_meet, bad = _clean_area(src.area_meeting_standard)
            changed |= bad
            s_uniq, bad = _clean_area(src.area_unique)
            changed |= bad
            # A source cannot cover more than the area; its meeting and unique
            # parts are both subsets of what it covers.
            s_cov_c = min(s_cov, total)
            s_meet_c = min(s_meet, s_cov_c)
            s_uniq_c = min(s_uniq, s_cov_c)
            changed |= (s_cov_c, s_meet_c, s_uniq_c) != (s_cov, s_meet, s_uniq)
            sources.append(SourceScore(str(src.source), s_cov_c, s_meet_c, s_uniq_c))

        try:
            cell_size = float(msg.cell_size)
        except (TypeError, ValueError):
            cell_size = float('nan')

        std = msg.area.standard
        stamp = msg.header.stamp
        stamp_ns = int(stamp.sec) * 1_000_000_000 + int(stamp.nanosec)
        return cls(
            area_id=str(msg.area.id),
            standard_name=str(std.name),
            standard_a=float(std.a),
            standard_b=float(std.b),
            cell_size=cell_size,
            total=total,
            covered=covered,
            meeting=meeting,
            stamp_ns=stamp_ns,
            sources=tuple(sources),
            clamped=changed,
        )


@dataclass
class StoredArea:
    """The latest score of one area and when (monotonic seconds) it arrived."""

    score: AreaScore
    received: float


class Store:
    """Keep the latest assessment per ``area.id``.

    The topic is latched with depth 100, so a late joiner gets up to 100
    assessments oldest first, with older repeats of an area before its newest.
    """

    def __init__(self):
        self._areas: Dict[str, StoredArea] = {}

    def update(self, score: AreaScore, now: float) -> bool:
        """Offer a score; return True when it became the held one for its area.

        A newer or equal ``header.stamp`` replaces the held score (equal: the
        last received wins, as for ``SurveyArea``).  An older stamp is dropped
        when it arrives within ``REPLAY_WINDOW_S`` of the held score, which is
        replay or reordering; later than that, a backwards stamp means the
        scorer's clock restarted and the message is accepted.
        """
        held = self._areas.get(score.area_id)
        if held is not None and score.stamp_ns < held.score.stamp_ns:
            if now - held.received <= REPLAY_WINDOW_S:
                return False
        self._areas[score.area_id] = StoredArea(score, now)
        return True

    def get(self, area_id: str) -> Optional[StoredArea]:
        return self._areas.get(area_id)

    def ids(self) -> List[str]:
        """Area ids seen so far, sorted."""
        return sorted(self._areas)

    def forget(self, area_id: str) -> None:
        self._areas.pop(area_id, None)

    def clear(self) -> None:
        self._areas.clear()

    def __len__(self) -> int:
        return len(self._areas)


@dataclass(frozen=True)
class Pooled:
    """Sum of several areas, in m^2.  Overlapping areas count a shared cell twice."""

    count: int
    total: float
    covered: float
    meeting: float

    @property
    def pct_covered(self) -> Optional[float]:
        return fraction(self.covered, self.total)

    @property
    def pct_to_spec(self) -> Optional[float]:
        return fraction(self.meeting, self.total)


def pool(scores: Iterable[AreaScore]) -> Pooled:
    """Area-weighted roll-up: a big area counts more than a small one.

    Areas with no total are skipped (they carry no weight).  Each area was
    judged against its own standard, and the message carries no overlap
    between areas, so this is a sum of areas, not a union.
    """
    count = 0
    total = covered = meeting = 0.0
    for s in scores:
        if s.total <= 0.0:
            continue
        count += 1
        total += s.total
        covered += s.covered
        meeting += s.meeting
    return Pooled(count, total, covered, meeting)


DEFAULT_WEIGHT_SPEC = 1.0
DEFAULT_WEIGHT_COVERED = 0.5


def valid_weight(value, default: float) -> float:
    """A finite weight in 0..1, else ``default``."""
    try:
        w = float(value)
    except (TypeError, ValueError):
        return default
    if not math.isfinite(w) or w < 0.0 or w > 1.0:
        return default
    return w


def combined_score(pct_covered: Optional[float], pct_to_spec: Optional[float],
                   weight_spec: float = DEFAULT_WEIGHT_SPEC,
                   weight_covered: float = DEFAULT_WEIGHT_COVERED
                   ) -> Optional[float]:
    """Combined score in percent of the area, or ``None`` if a ratio is missing.

    Cells that meet the standard count ``weight_spec``; cells covered but below
    the standard count ``weight_covered``.
    """
    if pct_covered is None or pct_to_spec is None:
        return None
    w_spec = valid_weight(weight_spec, DEFAULT_WEIGHT_SPEC)
    w_cov = valid_weight(weight_covered, DEFAULT_WEIGHT_COVERED)
    below = max(pct_covered - pct_to_spec, 0.0)
    return 100.0 * (w_spec * pct_to_spec + w_cov * below)


def aging_state(received: Optional[float], now: float, timeout: float) -> str:
    """``NEVER`` before any assessment, ``AGING`` past ``timeout``, else ``FRESH``.

    ``timeout <= 0`` disables aging.  Age is measured from receipt, not from
    ``header.stamp``, which is on the scorer's clock (sim time in the demo).
    The scorer publishes only when something changed, so ``AGING`` means "no
    new assessment", which is normal for an idle or finished survey.
    """
    if received is None:
        return NEVER
    if timeout > 0.0 and (now - received) > timeout:
        return AGING
    return FRESH


def format_age(seconds: float) -> str:
    """Human age: ``75 s``, ``3 min``, ``2 h``."""
    s = max(int(seconds), 0)
    if s < 120:
        return f'{s} s'
    if s < 7200:
        return f'{s // 60} min'
    return f'{s // 3600} h'


def format_pct(fraction_or_none: Optional[float]) -> str:
    """``87.3 %`` for a 0..1 fraction; ``--`` when unknown."""
    if fraction_or_none is None:
        return '--'
    return f'{100.0 * fraction_or_none:.1f} %'


def format_score(percent_or_none: Optional[float]) -> str:
    """``87.3 %`` for a value already in percent; ``--`` when unknown."""
    if percent_or_none is None:
        return '--'
    return f'{percent_or_none:.1f} %'


@dataclass
class CoverageConfig:
    """Settings persisted through rqt ``instance_settings`` as YAML."""

    topic: str = 'coverage_assessment'
    view: str = VIEW_GLANCE
    selected_areas: List[str] = field(default_factory=list)  # empty = all
    font_scale: float = 1.0
    show_combined: bool = False
    weight_spec: float = DEFAULT_WEIGHT_SPEC
    weight_covered: float = DEFAULT_WEIGHT_COVERED
    aging_timeout: float = 60.0  # seconds; 0 disables

    def normalized(self) -> 'CoverageConfig':
        """Return a copy with every field coerced to something usable."""
        defaults = CoverageConfig()
        topic = str(self.topic).strip() if self.topic else ''
        view = self.view if self.view in VIEWS else defaults.view
        try:
            scale = float(self.font_scale)
        except (TypeError, ValueError):
            scale = defaults.font_scale
        if not math.isfinite(scale):
            scale = defaults.font_scale
        scale = min(max(scale, FONT_SCALE_MIN), FONT_SCALE_MAX)
        try:
            timeout = float(self.aging_timeout)
        except (TypeError, ValueError):
            timeout = defaults.aging_timeout
        if not math.isfinite(timeout) or timeout < 0.0:
            timeout = defaults.aging_timeout
        if isinstance(self.selected_areas, (list, tuple)):
            areas = [str(a) for a in self.selected_areas if str(a)]
        else:
            areas = []
        return CoverageConfig(
            topic=topic or defaults.topic,
            view=view,
            selected_areas=areas,
            font_scale=scale,
            show_combined=bool(self.show_combined),
            weight_spec=valid_weight(self.weight_spec, DEFAULT_WEIGHT_SPEC),
            weight_covered=valid_weight(self.weight_covered, DEFAULT_WEIGHT_COVERED),
            aging_timeout=timeout,
        )

    def to_yaml(self) -> str:
        n = self.normalized()
        return yaml.safe_dump({f.name: getattr(n, f.name) for f in fields(n)},
                              sort_keys=True)

    @classmethod
    def from_yaml(cls, text: str) -> 'CoverageConfig':
        """Parse leniently: unknown keys are ignored, bad values fall back."""
        try:
            data = yaml.safe_load(text)
        except yaml.YAMLError:
            return cls()
        if not isinstance(data, dict):
            return cls()
        known = {f.name for f in fields(cls)}
        return cls(**{k: v for k, v in data.items() if k in known}).normalized()
