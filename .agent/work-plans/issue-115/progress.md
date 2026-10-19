---
issue: 115
---

# Issue #115 — rqt_ping_profile: draw in the platform's colour from /marine/platforms so the boat is identifiable at a glance

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-10-19 12:00 -04:00
**By**: Claude Code Agent (Claude Sonnet 5.5)
**Verdict**: approved

**Branch**: feature/issue-115
**Mode**: pre-push
**Depth**: Standard (reason: ~770 lines changed, live operator tooling)
**Must-fix**: 1 | **Suggestions**: 1
**Round**: 1 | **Ship**: recommended — the one must-fix and the suggestion are fixed; 145 tests pass. Reviewers: codex (ran), gemini (timed out at 420 s, no findings), own read. Claude subagent lenses not dispatched.

### Findings
- [x] (must-fix) platform identity used the configured topic, not the subscribed one (remap mislabels the boat) — now `subscription.topic_name`, test added — `ping_profile_widget.py`
- [x] (suggestion) `--color ''` could not clear a settings-file colour — fixed — `ping_profile_standalone.py`
- [x] (own, found in implementation) a one-slot newest-list mailbox dropped boats, since each boat publishes its own one-platform list; mailbox is per namespace, integration test covers it — `ping_profile_widget.py`
