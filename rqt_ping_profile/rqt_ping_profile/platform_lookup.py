"""Platform identity (name and colour) from ``/marine/platforms``, by topic namespace.

Pure Python, no Qt and no ROS imports, so any per-vehicle plugin can use it.
``marine_interfaces/PlatformList`` is read by attribute (``platforms[i].name``,
``.platform_namespace``, ``.color.r/g/b/a``).

A topic belongs to a platform when it lies under the platform's namespace:
``/urchin/sensors/mbes/detections`` belongs to the platform whose
``platform_namespace`` is ``urchin``.  A leading slash is optional on both
sides.  A topic with a single path segment (``detections``) has no namespace
and never matches.  When several namespaces match (``a`` and ``a/b``) the
longest wins.

Colour: red, green, blue and alpha are floats in 0..1.  As in CAMP, an alpha of
0 means "this platform has no colour"; the colour is then ``None`` (the name
still matches).  Alpha is otherwise only that switch: the colour is returned
opaque, because a display decides its own transparency.  A non-finite
component means no colour; an out-of-range one is clamped to 0..1.
"""

import math
import re
from dataclasses import dataclass
from typing import Dict, Iterable, Optional, Tuple

Rgba = Tuple[float, float, float, float]

_HEX = re.compile(r'^#?([0-9a-fA-F]{6})$')


@dataclass(frozen=True)
class PlatformMatch:
    """The platform a topic belongs to."""

    name: str
    namespace: str
    color: Optional[Rgba]       # opaque (r, g, b, 1.0) in 0..1, or None = no colour


def normalize_namespace(namespace) -> str:
    """``'/urchin/'`` -> ``'urchin'``; anything unusable -> ``''``."""
    if not isinstance(namespace, str):
        return ''
    return '/'.join(part for part in namespace.strip().split('/') if part)


def platform_color(color) -> Optional[Rgba]:
    """Colour of a ``std_msgs/ColorRGBA``-like value; ``None`` when alpha is 0 or unusable."""
    try:
        r, g, b, a = (float(color.r), float(color.g), float(color.b), float(color.a))
    except (AttributeError, TypeError, ValueError):
        return None
    if not all(math.isfinite(v) for v in (r, g, b, a)) or a <= 0.0:
        return None
    return (min(max(r, 0.0), 1.0), min(max(g, 0.0), 1.0), min(max(b, 0.0), 1.0), 1.0)


def parse_hex_color(text) -> Optional[Rgba]:
    """``'#ff8800'`` (or ``'ff8800'``) -> ``(1.0, 0.533.., 0.0, 1.0)``; ``None`` if not a hex colour."""
    if not isinstance(text, str):
        return None
    m = _HEX.match(text.strip())
    if not m:
        return None
    h = m.group(1)
    return (int(h[0:2], 16) / 255.0, int(h[2:4], 16) / 255.0, int(h[4:6], 16) / 255.0, 1.0)


def normalize_hex_color(text) -> str:
    """The canonical ``'#rrggbb'`` form of a hex colour, or ``''`` when it is not one."""
    if parse_hex_color(text) is None:
        return ''
    return '#' + text.strip().lstrip('#').lower()


def topic_in_namespace(topic, namespace) -> bool:
    """True when ``topic`` lies under ``namespace`` (an empty namespace never matches)."""
    ns = normalize_namespace(namespace)
    path = normalize_namespace(topic)
    return bool(ns) and path.startswith(ns + '/')


def _entry(platform) -> Optional[PlatformMatch]:
    ns = normalize_namespace(getattr(platform, 'platform_namespace', ''))
    if not ns:
        return None
    name = getattr(platform, 'name', '')
    return PlatformMatch(name=name.strip() if isinstance(name, str) else '',
                         namespace=ns, color=platform_color(getattr(platform, 'color', None)))


def match_platform(platforms: Iterable, topic) -> Optional[PlatformMatch]:
    """The platform among ``platforms`` (or a ``PlatformList``) that owns ``topic``."""
    items = getattr(platforms, 'platforms', platforms)
    best = None
    for platform in items or ():
        entry = _entry(platform)
        if entry and topic_in_namespace(topic, entry.namespace):
            if best is None or len(entry.namespace) > len(best.namespace):
                best = entry
    return best


class PlatformDirectory:
    """Platforms seen so far, by namespace.

    Each boat's ``platform_send`` publishes its own one-platform list on the same
    topic, so lists must be merged, not replaced: the last list holds one boat.
    A platform stays known until a later list names the same namespace again.
    """

    def __init__(self):
        self._by_namespace: Dict[str, PlatformMatch] = {}

    def update(self, platform_list) -> bool:
        """Merge a list; True when anything the lookup could return changed."""
        changed = False
        for platform in getattr(platform_list, 'platforms', None) or ():
            entry = _entry(platform)
            if entry and self._by_namespace.get(entry.namespace) != entry:
                self._by_namespace[entry.namespace] = entry
                changed = True
        return changed

    def lookup(self, topic) -> Optional[PlatformMatch]:
        """The known platform that owns ``topic``, or ``None``."""
        best = None
        for entry in self._by_namespace.values():
            if topic_in_namespace(topic, entry.namespace):
                if best is None or len(entry.namespace) > len(best.namespace):
                    best = entry
        return best

    def __len__(self):
        return len(self._by_namespace)
