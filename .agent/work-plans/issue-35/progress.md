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

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-05-26 09:59 -04:00
**By**: Claude Code Agent (Claude Opus 4.7 (1M context))
**Verdict**: approved (all findings addressed pre-push)

**Branch**: feature/issue-35 at `dfba3f5`
**Mode**: pre-push
**Depth**: Standard (reason: medium change to operator-safety display logic)
**Must-fix**: 0 | **Suggestions**: 0 (2 adversarial findings found + fixed before push)

### Findings
- [x] (fixed) whitespace-only threshold routed a healthy value to ERROR while the dialog validator showed it blank — `has_thresholds`/`evaluate_level` now strip (Claude adversarial) `dfba3f5`
- [x] (fixed) `to_dict` emitted whitespace-only thresholds while `has_thresholds` treated them absent — `to_dict` now strip-guards both branches; added omit test (Copilot adversarial) `dfba3f5`
- [x] static analysis: no project flake8/ruff config or ament lint tests; ad-hoc flake8 findings all pre-existing / out-of-profile; workspace pre-commit clean
- [x] governance: principles Pass; ADR-0008 (diagnostic_msgs KeyValue) + ADR-0013 (progress.md) compliant; echoboats#162 consumer + config_dialog consequences handled
- [x] plan drift: none — plan synced to landed code (4 source/test files match)

## Integrated Review
**Status**: complete
**When**: 2026-05-26 10:19 -04:00
**By**: Claude Code Agent (Claude Opus 4.7 (1M context))
**PR**: #36 at `a3f1c24`
**Sources**: Copilot PR review (1 inline comment)

### Findings
- [x] (valid, fixed) `value_key` matched verbatim — benign trailing space (e.g. `"Voltage "`) would fail the exact match and drive the row to ERROR. Normalized with `strip()` at match time, on serialization, and in flag text; added round-trip + whitespace-match tests. `config_model.py:select_keyvalue`/`to_dict` `a3f1c24`

## Integrated Review
**Status**: complete
**When**: 2026-05-26 10:44 -04:00
**By**: Claude Code Agent (Claude Opus 4.7 (1M context))

**PR**: #36 at `2bdf02a`
**Sources**: 1 (Copilot R1 @ `e8cc544` — now 3 commits stale)
**Cross-source confirmations**: 0
**CI**: no check-runs reported for this SHA (repo has no CI workflow)

Re-triage round (prompted while sequencing the BizzyBoat deploy: echoboats
#179 depends on this engine). No new reviews, inline comments, or
conversation comments since the prior Integrated Review. The sole Copilot
finding was already triaged last round and fixed in `a3f1c24`; confirmed the
fix is present at current HEAD. PR is mergeable / clean.

### Findings
- [x] (confirmed-addressed, Copilot) `value_key` whitespace — fix from `a3f1c24`
  verified at HEAD `2bdf02a` (`select_keyvalue` strips before exact match).
- [ ] (deploy-verification, hand-off to echoboats #179) Live `mavros: Battery`
  KeyValue **casing**: `select_keyvalue` is case-sensitive exact match by
  design (user-resolved Q2 = exact); the test publisher emits lowercase
  `voltage` while #179's config uses `value_key: Voltage`. If the boat's mavros
  publishes lowercase, #179's Battery row goes ERROR + `Voltage?`. Already
  flagged in `plan.md:97`; **verify the live key name on the boat before #179
  ships**. Not a #36 defect.
