---
issue: 66
---

# Issue #66 — rqt_operator_log: enlarge entry box to multi-line (QPlainTextEdit, Ctrl-Enter submit)

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-06-15 06:43 -04:00
**By**: Claude Code Agent (Claude Opus 4.8 (1M context))
**Verdict**: approved

**Branch**: feature/issue-66 at `d124d25` (uncommitted working tree)
**Mode**: pre-push
**Depth**: Light + both adversarial lenses (reason: small low-risk UI change, operator-facing)
**Must-fix**: 1 (fixed in-review) | **Suggestions**: 4

### Findings
- [x] (must-fix) Timeline flattened multi-line notes — HTML view collapses `\n`; added `_escape_multiline` (`\n`→`<br>`) + tests — `rqt_operator_log/log_widget.py:148-156`
- [ ] (suggestion) Ctrl+Shift/Alt+Enter also submit (`& Qt.ControlModifier`) — left as conventional lenient UX — `rqt_operator_log/log_widget.py:32`
- [ ] (suggestion) Entry min-height `3*lineSpacing()+12` is approximate (pre-style font, magic constant) — acceptable; splitter overrides — `rqt_operator_log/log_widget.py:86`
- [ ] (suggestion) Timeline has no min-height; can be dragged short — optional polish — `rqt_operator_log/log_widget.py`
- [ ] (suggestion) Enter→newline inverts old submit-on-Enter muscle memory — by design; mitigated by placeholder hint + button tooltip — `rqt_operator_log/log_widget.py:31`
