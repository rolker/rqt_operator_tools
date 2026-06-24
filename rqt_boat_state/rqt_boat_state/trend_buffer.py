"""Qt-free, time-binned ring buffer backing the TrendPlot sparklines.

Split out from ``trend_plot.py`` so the binning / window-sizing logic can be
unit-tested without importing Qt or needing a display.  The QPainter widget in
``trend_plot.py`` owns one of these and only reads from it.
"""

import math


class TrendBuffer:
    """Time-binned history of a scalar for a sparkline.

    Samples are folded into fixed-duration **time bins** (``bin_seconds``)
    rather than counted one-per-slot, so the span the plot covers is a real
    wall-clock window — ``capacity * bin_seconds`` — independent of how fast
    samples arrive.  The default 240 bins * 30 s = **2 h**.  (The previous
    sample-counted buffer spanned only seconds at a 75 Hz feed; binning fixes
    that.)

    Each finalized bin retains:

    * ``min`` / ``max`` of the samples folded into it — the plot's excursion
      band, so e.g. a battery's under-load voltage *sag* still shows; and
    * one ``agg`` value — the plot's line — selected per channel:

      - ``'max'``  peak in the bin (battery *resting* voltage; sag shows in band)
      - ``'min'``  trough
      - ``'mean'`` average
      - ``'last'`` most recent sample

    The currently-open (in-progress) bin is included in the readouts so live
    data appears without waiting a full ``bin_seconds`` to finalize.  Non-finite
    samples or timestamps are ignored so a NaN ``SoundSpeed`` cannot poison the
    trend.
    """

    _AGGS = ('max', 'min', 'mean', 'last')

    def __init__(self, capacity: int = 240, bin_seconds: float = 30.0,
                 agg: str = 'mean'):
        if capacity <= 0:
            raise ValueError('capacity must be positive')
        if bin_seconds <= 0:
            raise ValueError('bin_seconds must be positive')
        if agg not in self._AGGS:
            raise ValueError(f'agg must be one of {self._AGGS}, got {agg!r}')
        self._capacity = capacity
        self._bin_seconds = float(bin_seconds)
        self._agg = agg
        self._bins: list = []   # finalized (min, max, value), chronological
        self._cur = None        # open bin dict, or None

    @property
    def capacity(self) -> int:
        return self._capacity

    @property
    def bin_seconds(self) -> float:
        return self._bin_seconds

    @property
    def window_seconds(self) -> float:
        """Wall-clock span the buffer covers once full."""
        return self._capacity * self._bin_seconds

    def clear(self):
        self._bins = []
        self._cur = None

    def add(self, value, t):
        """Fold *value* (stamped at time *t*, seconds) into its time bin.

        Non-finite values/timestamps are silently ignored.  Crossing a bin
        boundary finalizes the open bin into the ring (dropping the oldest once
        over capacity) and opens a new one.
        """
        try:
            v = float(value)
            tt = float(t)
        except (TypeError, ValueError):
            return
        if not (math.isfinite(v) and math.isfinite(tt)):
            return

        bin_start = math.floor(tt / self._bin_seconds) * self._bin_seconds
        if self._cur is None:
            self._cur = self._new_bin(bin_start, v)
        elif bin_start > self._cur['t0']:
            self._finalize()
            self._cur = self._new_bin(bin_start, v)
        elif bin_start < self._cur['t0']:
            return  # backward-timestamped sample: drop it (don't rewrite history)
        else:
            # Same bin: fold it in.
            c = self._cur
            if v < c['min']:
                c['min'] = v
            if v > c['max']:
                c['max'] = v
            c['sum'] += v
            c['count'] += 1
            c['last'] = v

    @staticmethod
    def _new_bin(t0, v):
        return {'t0': t0, 'min': v, 'max': v, 'sum': v, 'count': 1, 'last': v}

    def _finalize(self):
        self._bins.append(self._bin_tuple(self._cur))
        if len(self._bins) > self._capacity:
            self._bins = self._bins[-self._capacity:]
        self._cur = None

    def _bin_tuple(self, c):
        if self._agg == 'max':
            val = c['max']
        elif self._agg == 'min':
            val = c['min']
        elif self._agg == 'last':
            val = c['last']
        else:  # mean
            val = c['sum'] / c['count']
        return (c['min'], c['max'], val)

    def _all(self) -> list:
        """Finalized bins plus the open bin, clipped to the window, oldest→newest."""
        bins = list(self._bins)
        if self._cur is not None:
            bins.append(self._bin_tuple(self._cur))
        return bins[-self._capacity:]

    def __len__(self) -> int:
        return len(self._all())

    def is_full(self) -> bool:
        return len(self._bins) >= self._capacity

    def minmax_pairs(self) -> list:
        """``(min, max)`` per bin, oldest→newest — the excursion band."""
        return [(mn, mx) for mn, mx, _ in self._all()]

    def values(self) -> list:
        """The per-bin aggregate (line), oldest→newest."""
        return [val for _, _, val in self._all()]

    def value_range(self):
        """Overall ``(min, max)`` across the window, or ``None`` when empty."""
        allbins = self._all()
        if not allbins:
            return None
        return (min(mn for mn, _, _ in allbins),
                max(mx for _, mx, _ in allbins))
