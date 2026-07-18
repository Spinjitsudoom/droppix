# Project skills (canonical)

Claude Code loads skills from this directory: `.claude/skills/<name>/SKILL.md`.

Cursor mirrors them via symlink:

```bash
mkdir -p .cursor/skills
ln -s ../../.claude/skills/<name> .cursor/skills/<name>
```

Do not create skill bodies under `.cursor/skills/` - only symlinks. See `.claude/rules/agent-tooling-sync.md`.
