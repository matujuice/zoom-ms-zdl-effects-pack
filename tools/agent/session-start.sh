#!/usr/bin/env bash
# Claude Code SessionStart hook: install and refresh the agent tools.
#   ponytail  - Claude Code plugin, enabled in .claude/settings.json (installed by Claude Code itself)
#   graphify  - code knowledge graph CLI (graphifyy on PyPI); graph built in the background
# Best effort: never fails the session.
set -u
cd "${CLAUDE_PROJECT_DIR:-$(dirname "$0")/../..}" || exit 0
GRAPHIFY_VERSION=0.9.80   # keep in step with .claude/skills/graphify/
export PATH="$HOME/.local/bin:$PATH"

# Cloud only: remove the old Gemini + RTK setup (until the environment's setup script stops installing it).
if [ "${CLAUDE_CODE_REMOTE:-}" = "true" ]; then
  python3 - "$HOME/.claude/settings.json" <<'PY'
import json, sys
p = sys.argv[1]
try:
    s = json.load(open(p))
except Exception:
    sys.exit()
pre = s.get("hooks", {}).get("PreToolUse", [])
keep = [h for h in pre if "rtk" not in json.dumps(h)]
if keep != pre:
    s["hooks"]["PreToolUse"] = keep
    json.dump(s, open(p, "w"), indent=2)
PY
  grep -qiE "ask-gemini|RTK" "$HOME/.claude/CLAUDE.md" 2>/dev/null && rm -f "$HOME/.claude/CLAUDE.md"
  rm -f /usr/local/bin/ask-gemini "$HOME/.local/bin/rtk" "$HOME/.claude/RTK.md" \
        "$HOME/.claude/hooks/rtk-rewrite.sh" "$HOME/.claude/hooks/.rtk-hook.sha256"
fi

# graphify: install the pinned version, then build or refresh the graph (AST only, no LLM, no API key).
if ! graphify --version 2>/dev/null | grep -q "$GRAPHIFY_VERSION"; then
  uv tool install --force "graphifyy==$GRAPHIFY_VERSION" >/dev/null 2>&1 \
    || python3 -m pip install --user -q "graphifyy==$GRAPHIFY_VERSION" >/dev/null 2>&1
fi
if command -v graphify >/dev/null 2>&1; then
  if [ -f graphify-out/graph.json ]; then
    nohup graphify update . >/dev/null 2>&1 &
  else
    nohup graphify extract . --code-only >/dev/null 2>&1 &
  fi
  echo "graphify $GRAPHIFY_VERSION ready; graph refreshing in graphify-out/ (use graphify query/path/explain before raw grep)."
else
  echo "graphify could not be installed this session; fall back to grep/Read."
fi
exit 0
