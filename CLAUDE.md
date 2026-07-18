# droppix — agent instructions

Spacedesk-like extended display for Linux (host C++/Qt6) + Android Kotlin client + optional Linux receive client.

**Start every session:** read [`scratchpad.md`](scratchpad.md), then [`docs/STATUS.md`](docs/STATUS.md) before trusting design-spec Status lines.

## Where rules and skills live

| Tool | Rules | Skills |
|---|---|---|
| **Claude Code (canonical)** | [`.claude/rules/`](.claude/rules/) | [`.claude/skills/`](.claude/skills/) |
| **Cursor (mirrors)** | [`.cursor/rules/*.mdc`](.cursor/rules/) → symlinks | [`.cursor/skills/<name>`](.cursor/skills/) → symlinks |

**Hard rule:** edit Claude paths only; Cursor entries must be symlinks. See [`.claude/rules/agent-tooling-sync.md`](.claude/rules/agent-tooling-sync.md). Verify with `bash scripts/check-agent-sync.sh`.

Always-on rules currently:

- [docs-maintenance](.claude/rules/docs-maintenance.md) — living docs update with code
- [agent-tooling-sync](.claude/rules/agent-tooling-sync.md) — keep Claude/Cursor tooling mirrored

## Docs hub

| Doc | Purpose |
|---|---|
| [`docs/STATUS.md`](docs/STATUS.md) | Living feature / design status |
| [`docs/WIRE.md`](docs/WIRE.md) | Wire protocol v5 |
| [`docs/README.md`](docs/README.md) | Docs index |
| [`README.md`](README.md) | Build / requirements |
| [`docs/lessons/INDEX.md`](docs/lessons/INDEX.md) | Indexed lessons |

## Spec → plan → build

New features: design under `docs/superpowers/specs/` → plan under `docs/superpowers/plans/` → implement → flip STATUS/spec Status to Shipped in the same change.
