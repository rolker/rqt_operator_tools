# AGENTS.md — rqt_operator_tools

Instructions for AI agents working in this repository — including **GitHub
Copilot code review**, which reads this file when reviewing PRs. Coding
agents: the deep guide (packages, layout, pitfalls) is
[`.agents/README.md`](.agents/README.md); read it before making changes,
alongside the per-plugin package READMEs.

## Workspace Rules

This repo is developed inside a
[ROS 2 Agent Workspace](https://github.com/rolker/ros2_agent_workspace).
The workspace root `AGENTS.md` carries the full shared rules (worktree
isolation, issue-first policy, commit conventions, AI signatures). This file
**references** those rules and adds repo-specific context only — it must
never restate or fork them.

## Quality Standard

This is software for autonomous robot boats operating on open water.
Robustness is not optional.

- Fix bugs completely: add the test, handle the edge case, check the
  lifecycle transition.
- Concerns about error handling, silent failures, stale data, or missing
  validation are not nits — flag them unless the failure mode genuinely
  cannot occur. "Config is under our control" and "pathological input" are
  not blanket dismissals; field configs change under pressure.
- A change includes its consequences: tests, documentation, and dependent
  references update in the same PR.

## Reviewing PRs

- If the PR carries a work plan (`.agent/work-plans/issue-<N>/plan.md` or a
  plan in the PR body), the plan is kept **in sync with the implementation
  as it evolves** — an implementation that matches the current plan text is
  not "plan drift", even if the plan changed after the PR opened.
- Verify claims against source: parameters, topics, services, and message
  types in docs must match the code.

## Review Context — rqt_operator_tools

- **Live operator-station tooling** (camera grid, sonar waterfall/echogram,
  annunciators, marine-control panels): a plugin crash during a survey costs
  scarce on-water time, so the robustness bar is high even for UI-polish
  changes — late-round hardening is in scope, not over-engineering.
- **Qt + ROS threading**: ROS callbacks and Qt widgets live on different
  threads; queued signal/slot connections and GUI-thread-only widget access
  are deliberate patterns — flag direct cross-thread widget calls.
- **Sensor subscribers use best_effort QoS** (sonar/camera streams);
  reliable-QoS assumptions silently receive nothing.
- **UI state persists via rqt perspectives**: user-facing settings (topic
  pickers, world frame, contact topic) follow the perspective-persisted
  toolbar-setting convention — new settings should too.
