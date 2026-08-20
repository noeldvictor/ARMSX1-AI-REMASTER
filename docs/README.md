# Documentation & Work Tracking

Every piece of work in this repo leaves a written trace. This is not
bureaucracy — it is the only thing that makes long-running autonomous AI
development reviewable. An agent that works for six hours and leaves one commit
message is unauditable; an agent that leaves a worklog is not.

## Directory map

| Directory | Holds | Written when |
| --- | --- | --- |
| `docs/research/` | Investigation, measurement, feasibility studies, prior-art reads. Conclusions with evidence. | Before committing to an approach. |
| `docs/decisions/` | ADRs. One irreversible-ish architectural choice each, with the alternatives that lost and why. | When a decision is made. |
| `docs/worklogs/` | What was actually done in a session: changed files, what worked, what broke, what is still open. | At the end of every working session. |
| `docs/archive/` | Dead material kept for reference. Nothing here runs. | On removal. |

## File naming

```
YYYYMMDD_HHMM_short-slug.md
```

Local time, 24-hour clock, kebab-case slug. Examples:

```
docs/research/20260820_1438_duckstation-feature-gap.md
docs/worklogs/20260820_1438_repo-bootstrap.md
docs/decisions/20260820_1450_remaster-architecture.md
```

Rationale: the prefix sorts chronologically in any file listing, is unambiguous
across timezones when paired with the worklog body, and never collides. Do not
renumber, rename, or delete these files after the fact. If a research doc turns
out to be wrong, write a new one that supersedes it and add a `Superseded by:`
line at the top of the old one. **The record is append-only.** A wrong
conclusion that was honestly reached and later corrected is more useful than a
tidy history.

## Required front matter

Every file in `research/`, `decisions/`, and `worklogs/` starts with:

```markdown
---
date: 2026-08-20 14:38
type: research | decision | worklog
status: draft | active | complete | superseded
tags: [gpu, vulkan, textures]
---
```

## Worklog template

```markdown
---
date: YYYY-MM-DD HH:MM
type: worklog
status: complete
tags: []
---

# <what this session was about>

## Goal
One sentence. What was this session trying to achieve?

## Done
- Concrete changes, with `file:line` references.

## Verified
How it was checked. Command run, output observed. If it was not verified,
say "NOT VERIFIED" — never imply otherwise.

## Broken / Known issues
Anything left in a worse state, or discovered and not fixed.

## Open questions
Things the next session needs answered.

## Next
The single most useful next action.
```

## The honesty rule

Worklogs record what happened, not what was intended. If the build failed, the
worklog says the build failed and pastes the error. If a feature was
half-finished, the worklog says which half. An agent that writes optimistic
worklogs poisons every future session that reads them, because future sessions
cannot re-run the past.
