"""Tests for threshold expression evaluation."""

import pytest

from rqt_operator_tools.config_model import (
    evaluate_threshold,
    validate_expression,
    preview_expression,
)


class TestEvaluateThreshold:
    def test_greater_than(self):
        assert evaluate_threshold('value > 10', 15) is True
        assert evaluate_threshold('value > 10', 5) is False

    def test_less_than(self):
        assert evaluate_threshold('value < 12.5', 12.0) is True
        assert evaluate_threshold('value < 12.5', 13.0) is False

    def test_equal(self):
        assert evaluate_threshold('value == 0', 0) is True
        assert evaluate_threshold('value == 0', 1) is False

    def test_not_equal(self):
        assert evaluate_threshold('value != 0', 1) is True

    def test_greater_equal(self):
        assert evaluate_threshold('value >= 10', 10) is True
        assert evaluate_threshold('value >= 10', 9) is False

    def test_less_equal(self):
        assert evaluate_threshold('value <= 10', 10) is True

    def test_chained_comparison(self):
        assert evaluate_threshold('10 < value < 20', 15) is True
        assert evaluate_threshold('10 < value < 20', 25) is False
        assert evaluate_threshold('10 < value < 20', 5) is False

    def test_boolean_and(self):
        assert evaluate_threshold('value > 10 and value < 20', 15) is True
        assert evaluate_threshold('value > 10 and value < 20', 25) is False

    def test_boolean_or(self):
        assert evaluate_threshold('value < 5 or value > 20', 3) is True
        assert evaluate_threshold('value < 5 or value > 20', 10) is False

    def test_not(self):
        assert evaluate_threshold('not value > 10', 5) is True
        assert evaluate_threshold('not value > 10', 15) is False

    def test_string_comparison(self):
        assert evaluate_threshold('value == "RTK"', 'RTK') is True
        assert evaluate_threshold('value == "RTK"', 'Float') is False

    def test_rejects_function_call(self):
        with pytest.raises(ValueError, match='Unsupported'):
            evaluate_threshold('len(value) > 0', 'test')

    def test_rejects_import(self):
        with pytest.raises(ValueError):
            evaluate_threshold('__import__("os")', 0)

    def test_rejects_attribute_access(self):
        with pytest.raises(ValueError, match='Unsupported'):
            evaluate_threshold('value.__class__', 0)

    def test_syntax_error(self):
        with pytest.raises(ValueError, match='Invalid expression syntax'):
            evaluate_threshold('value >', 0)


class TestValidateExpression:
    def test_valid(self):
        assert validate_expression('value > 10') is None
        assert validate_expression('value > 10 and value < 20') is None

    def test_invalid_syntax(self):
        result = validate_expression('value >')
        assert result is not None
        assert 'Syntax error' in result

    def test_unsafe_construct(self):
        result = validate_expression('len(value)')
        assert result is not None
        assert 'Unsupported' in result


class TestPreviewExpression:
    def test_pass(self):
        result = preview_expression('value > 10', 15)
        assert 'PASS' in result
        assert '15' in result

    def test_fail(self):
        result = preview_expression('value > 10', 5)
        assert 'FAIL' in result

    def test_error(self):
        result = preview_expression('value >', 5)
        assert 'Error' in result
