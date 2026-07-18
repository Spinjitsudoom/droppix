#!/usr/bin/env bash
# Verify Claude (canonical) ↔ Cursor (mirror) agent tooling stays symlinked in sync.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
fail=0

ok()   { printf 'OK   %s\n' "$*"; }
bad()  { printf 'FAIL %s\n' "$*"; fail=1; }

# Every .claude/rules/*.md must have a Cursor .mdc symlink with the same stem.
if [[ -d .claude/rules ]]; then
  shopt -s nullglob
  for rule in .claude/rules/*.md; do
    base="$(basename "$rule" .md)"
    [[ "$base" == "README" ]] && continue
    mirror=".cursor/rules/${base}.mdc"
    if [[ ! -e "$mirror" && ! -L "$mirror" ]]; then
      bad "missing Cursor mirror for $rule → expected $mirror"
      continue
    fi
    if [[ ! -L "$mirror" ]]; then
      bad "$mirror is a regular file; must be symlink to $rule"
      continue
    fi
    target="$(readlink "$mirror")"
    # Resolve relative to mirror's directory
    resolved="$(cd "$(dirname "$mirror")" && realpath "$target" 2>/dev/null || true)"
    expect="$(realpath "$rule")"
    if [[ -z "$resolved" || "$resolved" != "$expect" ]]; then
      bad "$mirror → $target (resolved='$resolved') does not point at $rule"
    else
      ok "$mirror → $rule"
    fi
  done
  shopt -u nullglob
fi

# Every Cursor rule must be a symlink into .claude/rules (no orphan Cursor-only rules).
if [[ -d .cursor/rules ]]; then
  shopt -s nullglob
  for mirror in .cursor/rules/*.mdc; do
    if [[ ! -L "$mirror" ]]; then
      bad "$mirror must be a symlink (Claude side is canonical)"
      continue
    fi
    target="$(readlink "$mirror")"
    resolved="$(cd "$(dirname "$mirror")" && realpath "$target" 2>/dev/null || true)"
    if [[ -z "$resolved" || "$resolved" != "$ROOT/.claude/rules/"* ]]; then
      bad "$mirror must resolve under .claude/rules/ (got '$resolved')"
    fi
  done
  shopt -u nullglob
fi

# Skills: each .claude/skills/<name>/ (dirs with SKILL.md) need Cursor symlink.
if [[ -d .claude/skills ]]; then
  shopt -s nullglob
  for skill_dir in .claude/skills/*/; do
    name="$(basename "$skill_dir")"
    [[ "$name" == "README.md" ]] && continue
    [[ -f "${skill_dir}SKILL.md" ]] || continue
    mirror=".cursor/skills/${name}"
    if [[ ! -e "$mirror" && ! -L "$mirror" ]]; then
      bad "missing Cursor skill mirror for $skill_dir → expected $mirror"
      continue
    fi
    if [[ ! -L "$mirror" ]]; then
      bad "$mirror is not a symlink; must link to .claude/skills/${name}"
      continue
    fi
    resolved="$(cd "$(dirname "$mirror")" && realpath "$(readlink "$mirror")" 2>/dev/null || true)"
    expect="$(realpath "$skill_dir")"
    if [[ -z "$resolved" || "$resolved" != "$expect" ]]; then
      bad "$mirror does not resolve to $skill_dir"
    else
      ok "$mirror → $skill_dir"
    fi
  done
  shopt -u nullglob
fi

# Orphan Cursor skills (must symlink into .claude/skills). Skip README.md notes.
if [[ -d .cursor/skills ]]; then
  shopt -s nullglob
  for mirror in .cursor/skills/*; do
    base="$(basename "$mirror")"
    [[ "$base" == "README.md" || "$base" == "README" ]] && continue
    [[ -e "$mirror" || -L "$mirror" ]] || continue
    if [[ ! -L "$mirror" ]]; then
      bad "$mirror must be a symlink into .claude/skills/"
      continue
    fi
    resolved="$(cd "$(dirname "$mirror")" && realpath "$(readlink "$mirror")" 2>/dev/null || true)"
    if [[ -z "$resolved" || "$resolved" != "$ROOT/.claude/skills/"* ]]; then
      bad "$mirror must resolve under .claude/skills/ (got '$resolved')"
    fi
  done
  shopt -u nullglob
fi

if [[ "$fail" -ne 0 ]]; then
  echo
  echo "Agent tooling out of sync. Claude paths are canonical; Cursor must be symlinks."
  echo "See .claude/rules/agent-tooling-sync.md"
  exit 1
fi
echo
echo "Agent tooling sync OK."
exit 0
