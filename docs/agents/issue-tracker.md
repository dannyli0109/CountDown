# Issue Tracker

Issues for this project live as YAML-frontmatter markdown files under `.scratch/` in the repo.

## Workflow

- Each issue is a `.md` file under `.scratch/<feature-name>/`
- The filename is the issue ID (e.g., `.scratch/flip-animation/001-frame-alignment.md`)
- The file contains frontmatter (YAML) + markdown description

## Creating an issue

```yaml
---
title: Flip animation frame alignment
labels: [needs-triage]
assignee: null
---

## Description
When the flip-card animates, the frame drifts slightly from the digit.

## Expected behavior
Frame and digit should remain locked together during the fold.
```

## Triage states

Skills assign one of these labels when processing:

- `needs-triage` — maintainer needs to evaluate
- `needs-info` — waiting on reporter
- `ready-for-agent` — fully specified, agent-ready
- `ready-for-human` — needs human implementation
- `wontfix` — will not be actioned

When ready to implement, convert the `.md` file to a branch or PR as needed.
