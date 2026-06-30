# Triage Labels

The `triage` skill uses these labels to track issue state through evaluation → ready → implementation.

| Label | Meaning |
|-------|---------|
| `needs-triage` | Maintainer needs to evaluate (new issue) |
| `needs-info` | Waiting on reporter to provide details |
| `ready-for-agent` | Fully specified; agent can pick up with no human context |
| `ready-for-human` | Needs human implementation |
| `wontfix` | Will not be actioned |

## Assignment flow

1. New issue → `needs-triage`
2. If needs clarification → `needs-info` (awaiting reporter)
3. Once fully specified → `ready-for-agent` or `ready-for-human`
4. If deprioritized → `wontfix`

These are the only labels the `triage` skill applies. Use them consistently.
