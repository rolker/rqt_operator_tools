"""Data model for annunciator indicator configuration."""

import ast
import operator
import re
from dataclasses import dataclass, field
from enum import Enum
from typing import Optional

import yaml


class MatchMode(Enum):
    """How to match diagnostic names."""
    SUBSTRING = 'substring'
    EXACT = 'exact'
    REGEX = 'regex'


class IndicatorLevel(Enum):
    """Status level for an indicator."""
    OK = 0
    WARN = 1
    ERROR = 2
    STALE = 3


# Operators allowed in threshold expressions.
_SAFE_OPS = {
    ast.Gt: operator.gt,
    ast.GtE: operator.ge,
    ast.Lt: operator.lt,
    ast.LtE: operator.le,
    ast.Eq: operator.eq,
    ast.NotEq: operator.ne,
}

_SAFE_BOOL_OPS = {
    ast.And: all,
    ast.Or: any,
}


def evaluate_threshold(expression: str, value) -> bool:
    """Evaluate a threshold expression with *value* in scope.

    Only comparison and boolean operators are allowed.  No function calls,
    attribute access, or subscript access.

    Raises ``ValueError`` on invalid expressions.
    """
    try:
        tree = ast.parse(expression, mode='eval')
    except SyntaxError as exc:
        raise ValueError(f'Invalid expression syntax: {exc}') from exc
    return _eval_node(tree.body, value)


def _eval_node(node: ast.AST, value):
    """Recursively evaluate a safe AST node."""
    if isinstance(node, ast.Compare):
        left = _eval_node(node.left, value)
        for op_node, comparator in zip(node.ops, node.comparators):
            op_type = type(op_node)
            if op_type not in _SAFE_OPS:
                raise ValueError(f'Unsupported operator: {ast.dump(op_node)}')
            right = _eval_node(comparator, value)
            if not _SAFE_OPS[op_type](left, right):
                return False
            left = right
        return True
    if isinstance(node, ast.BoolOp):
        op_type = type(node.op)
        if op_type not in _SAFE_BOOL_OPS:
            raise ValueError(f'Unsupported boolean operator: {ast.dump(node.op)}')
        if op_type is ast.And:
            return all(_eval_node(v, value) for v in node.values)
        return any(_eval_node(v, value) for v in node.values)
    if isinstance(node, ast.UnaryOp) and isinstance(node.op, ast.Not):
        return not _eval_node(node.operand, value)
    if isinstance(node, ast.Name) and node.id == 'value':
        return value
    if isinstance(node, ast.Constant):
        return node.value
    raise ValueError(
        f'Unsupported expression element: {ast.dump(node)}. '
        'Only comparisons, "and", "or", "not", "value", and literals are allowed.'
    )


def validate_expression(expression: str) -> Optional[str]:
    """Check if an expression is syntactically valid and safe.

    Returns ``None`` if valid, or an error message string.
    """
    try:
        tree = ast.parse(expression, mode='eval')
    except SyntaxError as exc:
        return f'Syntax error: {exc}'
    try:
        # Walk the AST to check for disallowed constructs.
        _check_safe(tree.body)
    except ValueError as exc:
        return str(exc)
    return None


def _check_safe(node: ast.AST):
    """Raise ``ValueError`` if *node* contains disallowed constructs."""
    if isinstance(node, ast.Compare):
        _check_safe(node.left)
        for op_node, comp in zip(node.ops, node.comparators):
            if type(op_node) not in _SAFE_OPS:
                raise ValueError(f'Unsupported operator: {ast.dump(op_node)}')
            _check_safe(comp)
    elif isinstance(node, ast.BoolOp):
        if type(node.op) not in _SAFE_BOOL_OPS:
            raise ValueError(f'Unsupported boolean operator: {ast.dump(node.op)}')
        for v in node.values:
            _check_safe(v)
    elif isinstance(node, ast.UnaryOp) and isinstance(node.op, ast.Not):
        _check_safe(node.operand)
    elif isinstance(node, ast.Name) and node.id == 'value':
        pass
    elif isinstance(node, ast.Constant):
        pass
    else:
        raise ValueError(
            f'Unsupported expression element: {ast.dump(node)}. '
            'Only comparisons, "and", "or", "not", "value", and literals are allowed.'
        )


def preview_expression(expression: str, current_value) -> str:
    """Return a human-readable preview of evaluating *expression*.

    Returns a string like ``"Current: 12.8 → PASS"`` or an error message.
    """
    err = validate_expression(expression)
    if err is not None:
        return f'Error: {err}'
    try:
        result = evaluate_threshold(expression, current_value)
        return f'Current: {current_value} → {"PASS" if result else "FAIL"}'
    except Exception as exc:
        return f'Error: {exc}'


@dataclass
class IndicatorConfig:
    """Configuration for a single annunciator indicator."""

    name: str
    source: str = 'topic'  # 'topic' or 'diagnostics'

    # Topic source fields.
    topic: str = ''
    msg_type: str = ''
    value_field: str = 'data'
    format: str = '{}'

    # Threshold expressions (Python expressions with ``value`` in scope).
    threshold_warn: str = ''
    threshold_error: str = ''

    # Diagnostics source fields.
    diagnostic_name: str = ''
    match_mode: MatchMode = MatchMode.SUBSTRING

    # Timing.
    stale_timeout: float = 5.0
    stale_error_timeout: float = 0.0  # 0 = 3× stale_timeout

    def matches_diagnostic(self, status_name: str) -> bool:
        """Check if *status_name* matches this indicator's diagnostic filter."""
        if not self.diagnostic_name:
            return False
        if self.match_mode == MatchMode.EXACT:
            return status_name == self.diagnostic_name
        if self.match_mode == MatchMode.REGEX:
            try:
                return bool(re.search(self.diagnostic_name, status_name))
            except re.error:
                return False
        # Default: substring.
        return self.diagnostic_name in status_name

    def evaluate_level(self, value) -> IndicatorLevel:
        """Determine the indicator level from a raw value using thresholds.

        Evaluates error first, then warn.  If no thresholds match or none
        are configured, returns OK.
        """
        try:
            if self.threshold_error and evaluate_threshold(self.threshold_error, value):
                return IndicatorLevel.ERROR
            if self.threshold_warn and evaluate_threshold(self.threshold_warn, value):
                return IndicatorLevel.WARN
        except (ValueError, TypeError):
            return IndicatorLevel.ERROR
        return IndicatorLevel.OK

    def to_dict(self) -> dict:
        """Serialize to a dictionary for YAML output."""
        d = {'name': self.name, 'source': self.source}
        if self.source == 'diagnostics':
            d['diagnostic_name'] = self.diagnostic_name
            if self.match_mode != MatchMode.SUBSTRING:
                d['match_mode'] = self.match_mode.value
            if self.format != '{}':
                d['format'] = self.format
        else:
            d['topic'] = self.topic
            d['msg_type'] = self.msg_type
            if self.value_field != 'data':
                d['value_field'] = self.value_field
            d['format'] = self.format
            thresholds = {}
            if self.threshold_warn:
                thresholds['warn'] = self.threshold_warn
            if self.threshold_error:
                thresholds['error'] = self.threshold_error
            if thresholds:
                d['thresholds'] = thresholds
        if self.stale_timeout != 5.0:
            d['stale_timeout'] = self.stale_timeout
        if self.stale_error_timeout != 0.0:
            d['stale_error_timeout'] = self.stale_error_timeout
        return d

    @classmethod
    def from_dict(cls, d: dict) -> 'IndicatorConfig':
        """Deserialize from a YAML-loaded dictionary."""
        thresholds = d.get('thresholds', {})
        match_mode_str = d.get('match_mode', 'substring')
        try:
            match_mode = MatchMode(match_mode_str)
        except ValueError:
            match_mode = MatchMode.SUBSTRING
        return cls(
            name=d.get('name', 'Unnamed'),
            source=d.get('source', 'topic'),
            topic=d.get('topic', ''),
            msg_type=d.get('msg_type', ''),
            value_field=d.get('value_field', 'data'),
            format=d.get('format', '{}'),
            threshold_warn=thresholds.get('warn', ''),
            threshold_error=thresholds.get('error', ''),
            diagnostic_name=d.get('diagnostic_name', ''),
            match_mode=match_mode,
            stale_timeout=d.get('stale_timeout', 5.0),
            stale_error_timeout=d.get('stale_error_timeout', 0.0),
        )


@dataclass
class AnnunciatorConfig:
    """Top-level annunciator configuration."""

    diagnostics_topic: str = '/diagnostics'
    indicators: list = field(default_factory=list)

    def to_yaml(self) -> str:
        d = {
            'diagnostics_topic': self.diagnostics_topic,
            'indicators': [ind.to_dict() for ind in self.indicators],
        }
        return yaml.dump(d, default_flow_style=False, sort_keys=False)

    @classmethod
    def from_yaml(cls, text: str) -> 'AnnunciatorConfig':
        d = yaml.safe_load(text) or {}
        indicators = [
            IndicatorConfig.from_dict(item)
            for item in d.get('indicators', [])
        ]
        return cls(
            diagnostics_topic=d.get('diagnostics_topic', '/diagnostics'),
            indicators=indicators,
        )

    @classmethod
    def from_file(cls, path: str) -> 'AnnunciatorConfig':
        with open(path) as f:
            return cls.from_yaml(f.read())

    def to_file(self, path: str):
        with open(path, 'w') as f:
            f.write(self.to_yaml())
