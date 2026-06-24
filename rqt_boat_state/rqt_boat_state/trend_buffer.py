"""Qt-free ring buffer backing the TrendPlot sparklines.

Split out from ``trend_plot.py`` so the wraparound / min-max / window-sizing
logic can be unit-tested without importing Qt or needing a display.  The
QPainter widget in ``trend_plot.py`` owns one of these and only reads from it.
"""

import math


class TrendBuffer:
    """Fixed-capacity ring buffer of (min, max) samples for a sparkline.

    Capacity is counted in *samples* (slots), not wall-clock time: the
    default 720 holds the most recent 720 pushes.  The wall-clock window the
    buffer spans therefore depends on how fast the owner pushes — at a
    nominal 1 sample / 10 s it would be ≈2 h (720 × 10 s), but the boat-state
    panel currently pushes once per received message (no decimation), so the
    real span tracks message arrival rate.  Each slot keeps the min and max
    of the values folded into it so a decimated plot still shows excursions;
    with one ``push`` per slot min == max == the pushed value.

    Non-finite samples (NaN/inf) are ignored so a NaN ``SoundSpeed`` does not
    poison the trend.
    """

    def __init__(self, capacity: int = 720):
        if capacity <= 0:
            raise ValueError('capacity must be positive')
        self._capacity = capacity
        self._mins: list = []
        self._maxs: list = []
        self._start = 0  # index of the oldest sample once full

    @property
    def capacity(self) -> int:
        return self._capacity

    def __len__(self) -> int:
        return len(self._mins)

    def is_full(self) -> bool:
        return len(self._mins) >= self._capacity

    def clear(self):
        self._mins.clear()
        self._maxs.clear()
        self._start = 0

    def push(self, value, vmax=None):
        """Append a sample, dropping the oldest once at capacity.

        With a single argument the slot's min and max are both *value*.  Pass
        *vmax* to fold a pre-aggregated (min, max) pair into one slot.  Non-
        finite samples are silently ignored.
        """
        vmin = value
        if vmax is None:
            vmax = value
        try:
            vmin = float(vmin)
            vmax = float(vmax)
        except (TypeError, ValueError):
            return
        if not (math.isfinite(vmin) and math.isfinite(vmax)):
            return
        if vmin > vmax:
            vmin, vmax = vmax, vmin

        if not self.is_full():
            self._mins.append(vmin)
            self._maxs.append(vmax)
            return
        # Full: overwrite the oldest slot and advance the ring start.
        self._mins[self._start] = vmin
        self._maxs[self._start] = vmax
        self._start = (self._start + 1) % self._capacity

    def values(self) -> list:
        """Mid-points oldest→newest (``(min+max)/2`` per slot)."""
        return [(lo + hi) * 0.5 for lo, hi in self._pairs()]

    def minmax_pairs(self) -> list:
        """``(min, max)`` per slot, oldest→newest."""
        return list(self._pairs())

    def range(self):
        """Overall ``(min, max)`` across the buffer, or ``None`` when empty."""
        if not self._mins:
            return None
        return (min(self._mins), max(self._maxs))

    def _pairs(self):
        """Yield (min, max) slots in chronological (oldest→newest) order."""
        n = len(self._mins)
        if n < self._capacity:
            # Not yet wrapped: stored in insertion order.
            for i in range(n):
                yield self._mins[i], self._maxs[i]
        else:
            for k in range(self._capacity):
                i = (self._start + k) % self._capacity
                yield self._mins[i], self._maxs[i]
