---
issue: 76
---

# Issue #76 — rqt_sonar_waterfall: scroll a ring texture instead of full re-upload per ping

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-06-16 03:08 +00:00
**By**: Claude Code Agent (Claude Opus 4.8)
**Verdict**: changes-requested

**Branch**: feature/issue-76 at `6bfe709`
**Mode**: pre-push
**Depth**: Standard (reason: ~191 lines across 4 files; subtle GPU ring-buffer index math + GL resource lifecycle)
**Must-fix**: 1 | **Suggestions**: 4

### Findings
- [ ] (must-fix) `upload_texture` commits `tex_width_`/`tex_capacity_` inside the realloc block *before* the upload-error check, so a `glTexImage2D` (OOM) failure caches the dims as if allocation succeeded; the next `upload_texture` then skips reallocation and never retries — a permanent no-data state until geometry changes. Regresses the prior unconditional-`glTexImage2D` auto-recovery. Fix: commit dims (and reset `ring_write_`) only after `upload_err == GL_NO_ERROR`; reset `tex_width_`/`tex_capacity_` to 0 on the error path. — `rqt_sonar_waterfall/src/waterfall_widget.cpp:478-499`
- [ ] (suggestion) `append_rows` recomputes `max_w`/`uniform_half` over the *whole* buffer, but `upload_texture` bakes them from only the newest-`capacity` window; when `buffer_.capacity() > GL_MAX_TEXTURE_SIZE` this forces a full rebuild every ping, silently defeating the fast path (perf-only, extreme-history edge). — `rqt_sonar_waterfall/src/waterfall_widget.cpp:525-536`
- [ ] (suggestion) Ring-state reset is non-uniform across early exits: empty exit resets `ring_filled_`+`ring_write_`, but alloc-fail/upload-fail exits reset only `ring_filled_`. Reset `ring_write_` (and the cached tex dims) on all exits for a uniform invariant. — `rqt_sonar_waterfall/src/waterfall_widget.cpp:426,497`
- [ ] (suggestion) Memory footprint: ring is now allocated to full history capacity (`width × capacity`) up front rather than growing with row count. Bounded (history ≤ 5000, width ≤ GL_MAX_TEXTURE_SIZE) but a real startup/steady VRAM increase at large history; worth a note in the PR description. — `rqt_sonar_waterfall/src/waterfall_widget.cpp:382,472`
- [ ] (suggestion) Unused ring rows are uploaded with `nullptr` and never cleared; safety relies entirely on `u_ring_filled` exactly matching written rows + `GL_NEAREST`. A defensive zero-fill on (re)allocation would make the worst case "black" not uninitialized VRAM. — `rqt_sonar_waterfall/src/waterfall_widget.cpp:467-483`

### Notes
- Static analysis (ament_cpplint, ament_cppcheck): clean on all 4 changed files.
- Ring index math verified correct by both manual trace and Lens A across empty / partial / exactly-full / wrapped / burst-with-eviction fill levels, including the wrap seam. `oldest = (ring_write_ + cap - ring_filled_) % cap` is derived relative to `ring_write_`, so incremental appends of only the newest N rows leave the older `cap-N` slots correctly resident.
- GL state hygiene (bind/unbind pairing, uniform completeness incl. `flip_v`/LUT/ring, texparam-on-realloc, unpack alignment) verified sound by Lens B.
- Governance: project repo has no `.agents/`, `docs/PRINCIPLES.md`, or `review-context.yaml` — workspace principles only; change is self-contained to one package (no ADR/consequence triggers). No work plan exists, so Plan Drift was skipped.
- Happy path is solid; the single must-fix is an error-recovery-path regression that only manifests on GPU allocation failure.
</content>
</invoke>
