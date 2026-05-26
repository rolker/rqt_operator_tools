---
issue: 35
---

# Issue #35 — rqt_annunciator: value thresholds + KeyValue selection on diagnostics-source indicators

## Plan Authored
**Status**: complete
**When**: 2026-05-26 09:15 -04:00
**By**: Claude Code Agent (Claude Opus 4.7 (1M context))

**Plan**: `.agent/work-plans/issue-35/plan.md` at `5ca5b1a`
**PR**: https://github.com/rolker/rqt_operator_tools/pull/36 (`[PLAN]` prefix)
**Phases**: single

### Open questions
- [x] Q1 — degraded data (missing/non-numeric KeyValue): **resolved → escalate to ERROR + flag text** (user, 2026-05-26)
- [x] Q2 — `value_key` matching: **resolved → exact** (user, 2026-05-26)

## Plan Review
**Status**: complete
**When**: 2026-05-26 09:15 -04:00
**By**: Claude Code Agent (Claude Opus 4.7 (1M context)) — delegated independent sub-agent (fresh context)

**Plan**: `.agent/work-plans/issue-35/plan.md` at `61bbeb3` (post-amendment)
**PR**: https://github.com/rolker/rqt_operator_tools/pull/36
**Verdict**: changes-requested → all addressed in `61bbeb3`

### Findings
- [x] (must-fix) `config_dialog.py` `get_config` rebuilds diagnostics rows with no `value_key`; once `to_dict` emits diagnostics thresholds, GUI edits silently drop `value_key` — brought dialog into scope (round-trip `value_key`/thresholds) `61bbeb3`
- [x] (must-fix) degraded-data path must wrap `float(selected)` and route missing-key + `ValueError` to ERROR; not via `evaluate_level` string coercion — Approach step 3 specified `61bbeb3`
- [x] (suggestion) document deliberate STALE-masks-threshold-ERROR semantics + producer-STALE is the only STALE `combine_levels` sees; add STALE×threshold test `61bbeb3`
- [x] (suggestion) expand combine test matrix (OK/WARN/ERROR/STALE × value) + no-thresholds backward-compat assertion `61bbeb3`
- [x] (suggestion) note live `value_key` casing check (publisher uses lowercase `voltage`) before echoboats consumer ships `61bbeb3`
