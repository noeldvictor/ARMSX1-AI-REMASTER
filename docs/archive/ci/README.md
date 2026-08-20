# Archived CI

Nothing in this directory runs. It is kept for reference only.

## Why it was archived

`ARMSX1-AI-REMASTER` is a personal experimental fork. It has no release
obligations, no contributors to gate, and no budget for hosted runners. Paying
for cloud CI minutes to build six platform targets that the remaster work does
not touch is not a good use of anything.

## What was archived

| Was | Now | Notes |
| --- | --- | --- |
| `.gitea/workflows/build.yml` | `docs/archive/ci/gitea-build.yml` | 567-line Gitea act-runner matrix. Never ran on GitHub — GitHub only reads `.github/workflows/`, so this was already inert here. |
| `.github/FUNDING.yml` | *deleted* | Pointed donations at the upstream `psxe` author's Buy Me a Coffee. Soliciting funds from a fork's page on the original author's behalf is not something this fork should do. |

There were never any `.github/workflows/` files in this repo, so no GitHub
Actions minutes were ever being consumed.

## What replaced it

Local verification only. See `CLAUDE.md` for the gate that every change runs:

```sh
python3 tests/run_validation.py
```

The archived matrix is still a useful reference for the exact packages and
cross-compile flags each platform target needs. If a real release ever matters,
start from that file rather than rewriting it.
