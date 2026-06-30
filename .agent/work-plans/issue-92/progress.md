---
issue: 92
---

# Issue #92 — rqt_marine_control: grouping, range hints, multi-device tabs

## Issue Review
**Status**: complete
**When**: 2026-06-30 00:00 +00:00
**By**: Claude Code Agent (Claude Sonnet)

**Issue**: #92
**Comment**: (best-effort post follows this entry; not recorded inline)
**Scope verdict**: well-scoped

### Scope Assessment

Three closely related UX improvements to two packages in the same project repo
(`rqt_marine_control` + `marine_control_widgets`). All changes are contained within
`rqt_operator_tools`. The third feature (multi-device tabs) is the most complex and
introduces a subscription lifecycle change; the issue appropriately defers the
design question to plan-task. A single PR is feasible but will be substantial.

**Right repo?** Yes — changes are entirely within the `rqt_operator_tools` project repo.

**Dependencies?** No blocking dependencies. `unh_marine_navigation#84` is motivation
context; issue #78 (deferred DDS queries) is existing behavior to preserve.

### Principle Alignment

| Principle | Status | Notes |
|---|---|---|
| Human control and transparency | OK | All three improvements directly increase operator visibility (device name, bounds, multi-device) |
| Enforcement over documentation | OK | No new compliance rules proposed |
| Capture decisions, not just implementations | Watch | The per-tab subscription map architecture is a non-trivial design; plan should document the lifecycle design clearly |
| A change includes its consequences | Watch | Tab teardown / settings save-restore for multi-tab state should be explicitly covered, not just "where practical" |
| Only what's needed | OK | Three focused UX improvements targeting a concrete operator pain (visible after unh_marine_navigation#84) |
| Improve incrementally | Watch | Three features in one PR is slightly large; splitting tab lifecycle is possible but they are cohesive; acceptable if plan keeps each change reviewable |
| Test what breaks | Action needed | Subscription lifecycle teardown (no leaked subs, no cross-talk) is exactly the class of bug hard to catch in the field — tests must be required, not optional |
| Workspace vs. project separation | OK | No workspace-level changes |

### ADR Applicability

| ADR | Triggered | Notes |
|---|---|---|
| 0001 — Adopt ADRs | Watch | The per-tab subscription map is a notable design change; if the approach is non-obvious, capture it in plan rationale or a lightweight ADR addendum |
| 0002 — Worktree isolation | Yes | Feature work uses the worktree — already in place |
| 0008 — Follow ROS 2 Conventions | Yes | New per-tab publishers/subscribers must be created and destroyed cleanly; ensure node lifecycle follows ROS 2 conventions |
| 0013 — progress.md vocabulary | Yes | This entry |

### Consequences

- If `ControlSetWidget`'s constructor or public API changes to support grouping,
  any callers outside `rqt_marine_control` (check for other plugins using it) need updating.
- Settings save/restore must be extended to persist multi-tab state (active tab,
  per-tab topic). This is called out in the acceptance criteria but should be
  explicitly planned.
- The acceptance criterion "Existing behaviors preserved: no-op-edit suppression,
  signal-blocking on refresh, deferred DDS queries (#78), settings save/restore"
  should map to explicit test cases in the plan.

### Recommendations

- Require (not "where practical") tests for the tab subscription lifecycle: verify
  no leaked subs after tab close, no cross-talk between tabs with simultaneous
  updates.
- Clarify in planning whether a manually-selected topic and a bridge-connected device
  can coexist as tabs, or whether they are mutually exclusive modes.
- Consider splitting implementation into two PRs: (1) grouping + range hints (lower
  risk, pure widget changes) and (2) multi-device tabs (subscription lifecycle change).
  This is a Recommendation, not a blocking concern — if the team prefers one PR, the
  issue is still plan-task-ready.

### Actions
- [ ] Require tests for tab subscription lifecycle (no leaked subs, no cross-talk) — not optional.
- [ ] Plan must document the per-tab subscription map lifecycle design (create/destroy semantics, settings persistence for multi-tab state).
- [ ] Verify no other callers of `ControlSetWidget` public API exist that would break if the constructor/API changes.
