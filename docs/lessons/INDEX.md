# Lessons index

## How to use

1. Grep this file for a failure tag (e.g. `silent-failure`, `oom`) or subsystem tag (e.g. `encoder`, `protocol`).
2. Open **only** the matching detail file(s). Do not load every lesson.

## Tag taxonomy

- **Subsystem:** `host`, `android`, `client`, `protocol`, `encoder`, `evdi`, `input`, `transport`, `gui`, `packaging`, `desktop-backend`, `audio`, `tls`
- **Failure class:** `silent-failure`, `data-loss`, `performance`, `oom`, `truncation`, `wrong-answer`, `regression`, `flake`, `gotcha`
- **Severity:** `critical`, `high`, `medium`, `low`
- **Doc type:** `lesson`, `constraint`, `gotcha`

## Lessons

| ID | Title | Tags | Severity | Date | File |
|---|---|---|---|---|---|
| *(none yet)* | — | — | — | — | — |

## Related external docs

- [../STATUS.md](../STATUS.md) — feature / design status
- [../WIRE.md](../WIRE.md) — current protocol
- [../../scratchpad.md](../../scratchpad.md) — session memory
- [../README.md](../README.md) — docs hub

## Adding a new lesson

Copy [`_TEMPLATE.md`](_TEMPLATE.md). Use IDs:

- `L-YYYY-MM-DD-shortslug` — fixed mistakes
- `C-YYYY-MM-DD-shortslug` — hard constraints
- `G-YYYY-MM-DD-shortslug` — gotchas

Add one row to the table above. Keep the detail file short; link to code/commits.
