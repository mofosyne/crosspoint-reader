# ZIM reader handover (TEMPORARY)

> Temporary working notes for moving this project between Claude Code sessions.
> Delete this whole `handover/` folder before opening any upstream pull request.

## One-paragraph summary

The goal is an on-device ZIM reader (offline Wikipedia from a stock Kiwix ZIM on the SD
card) that sits next to the normal ebook reading, on newer PSRAM boards (reTerminal
Sticky first). A host-tested ZIM reader library exists and a hardware spike on the
Sticky shows it is fast enough: opening an article costs about 0.35 s before layout.
The next work is porting the spike into the real firmware as a feature-gated build,
a minimal browse screen, and an article viewer that reuses the dictionary viewer.

## Read these in order

1. `01-goal-and-rules.md` - goal, decisions already made, working rules, user preferences
2. `02-findings.md` - ZIM format facts, archive survey, renderer findings, hardware facts
3. `03-measurements.md` - host read counts and the device spike results (raw log in `logs/`)
4. `04-state-and-next-steps.md` - branches, commits, file inventory, how to build and
   test, ordered next steps, risks, questions for the user

`tools/` has the throwaway probe scripts used to get the findings. `logs/` has raw output (kept partly for a possible blog post; see `05-blog-notes.md`).

## What this session could not carry over

- The user's ZIM files (local only) and the Sticky itself. A cloud session cannot flash
  or read the device. Anything that needs them is a request to the user, with exact
  commands.
- The previous session's private notes. Everything useful is in this folder.

## First five minutes for the next session

1. `git status`, `git branch -vv`, `git submodule status` (freeink-sdk must be at the pinned commit).
2. Read this folder, then `AGENTS.md` and `CLAUDE.md` in the repo root.
3. Run the host tests (commands in `04-state-and-next-steps.md`) to confirm the baseline.
4. Ask the user which next step to take. Do not commit or push without their explicit say-so.
