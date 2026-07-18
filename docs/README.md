# droppix documentation

Start here:

| Doc | Purpose |
|---|---|
| [STATUS.md](STATUS.md) | **Living** feature / design status (overrides stale Status lines in older specs) |
| [WIRE.md](WIRE.md) | Current wire protocol (v5) summary |
| [../README.md](../README.md) | Build, requirements, layout |
| [../scratchpad.md](../scratchpad.md) | Session memory for agents |
| [lessons/INDEX.md](lessons/INDEX.md) | Indexed lessons / constraints |
| [../CLAUDE.md](../CLAUDE.md) | Agent entry (Claude Code) |
| [../AGENTS.md](../AGENTS.md) | Short agent entrypoint |
| [../.claude/rules/](../.claude/rules/) | **Canonical** project rules (docs + tooling sync) |
| [../.cursor/rules/](../.cursor/rules/) | Symlinks → `.claude/rules/` (Cursor) |
| [../scripts/check-agent-sync.sh](../scripts/check-agent-sync.sh) | Fail if Claude/Cursor mirrors drift |

**Agents:** living docs are part of every feature/protocol change. Edit rules under `.claude/rules/` only; keep Cursor as symlinks (`bash scripts/check-agent-sync.sh`).

## Design workflow

Feature work follows **spec → plan → build**:

- `superpowers/specs/` — approved designs (historical; check [STATUS.md](STATUS.md) for ship state)
- `superpowers/plans/` — phased implementation checklists (build journals)

Do not treat a spec header that still says "implementation plan pending" as authoritative if `STATUS.md` says **Shipped**.

## Open roadmap (high level)

1. **Cross-desktop M2/M3** — Sway/wlroots and GNOME/Mutter backends (`2026-07-05-cross-desktop-portability-design.md`). M1 seam + X11 backend already shipped.
2. **Optional polish** — expose `--encoder` in the host GUI; further latency work (zero-copy capture remains out of scope for now).
