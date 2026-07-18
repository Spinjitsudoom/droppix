---
description: Keep Claude Code and Cursor project rules/skills in sync (symlink, do not fork)
alwaysApply: true
---

# Claude ↔ Cursor agent tooling sync

droppix is used from **Claude Code** and **Cursor**. Project rules and skills must stay identical. **Claude paths are canonical; Cursor paths are mirrors (symlinks).**

## Canonical layout

```
CLAUDE.md                         # short entry (identity + pointers) — Claude Code
AGENTS.md                         # short pointer at CLAUDE.md

.claude/rules/<name>.md           # CANONICAL always-on / path rules
.claude/skills/<name>/SKILL.md    # CANONICAL project skills

.cursor/rules/<name>.mdc          # MUST be symlink → ../../.claude/rules/<name>.md
.cursor/skills/<name>             # MUST be symlink → ../../.claude/skills/<name>
```

## Hard rules

1. **Never duplicate bodies.** Do not copy-paste a rule or skill into both trees.
2. **Add on the Claude side first**, then create the Cursor symlink in the same change.
3. **Edit only the Claude path** (follow symlinks / edit the target). If you edit through the Cursor path, you are still editing the same inode - fine - but never replace a symlink with a regular file.
4. **New always-on rule:** write `.claude/rules/<name>.md`, then:
   `ln -s ../../.claude/rules/<name>.md .cursor/rules/<name>.mdc`
5. **New project skill:** write `.claude/skills/<name>/SKILL.md` (+ helpers), then:
   `ln -s ../../.claude/skills/<name> .cursor/skills/<name>`
6. **Frontmatter for dual loaders:**
   - Always-on: Cursor `alwaysApply: true`; Claude = no `paths` key (loads every session).
   - Path-scoped: include **both** Cursor `globs:` and Claude `paths:` with the same patterns; set `alwaysApply: false`.
7. **Personal** skills/rules (`~/.cursor/skills`, `~/.claude/...`) are out of scope - do not put personal tooling in the repo mirrors.
8. Before finishing any change that adds/renames rules or skills, run:
   `bash scripts/check-agent-sync.sh`

## Forbidden

```
# BAD - forked copies
.claude/rules/foo.md   (body A)
.cursor/rules/foo.mdc  (body B, regular file)

# GOOD
.claude/rules/foo.md   (only body)
.cursor/rules/foo.mdc -> ../../.claude/rules/foo.md
```

## Verify

```bash
bash scripts/check-agent-sync.sh
```

Must exit 0. Fix any "not a symlink" / "broken target" / "missing mirror" failures before merge.
