#!/bin/bash
# TDD Guard: block edits to src/ or include/ unless tests/ has been modified first.
# Receives PreToolUse JSON on stdin.

set -euo pipefail

INPUT=$(cat)
FILE=$(echo "$INPUT" | jq -r '.tool_input.file_path // .tool_input.filePath // empty')

REPO_ROOT=$(cd "$(dirname "$0")/../.." && pwd)

# Get path relative to repo root
REL_PATH="${FILE#"$REPO_ROOT"/}"

# Only guard src/ and include/ edits (relative to repo root)
case "$REL_PATH" in
  src/*|include/*) ;;
  *) exit 0 ;;
esac

# Check if any test file has uncommitted changes (staged or unstaged)
TEST_CHANGES=$(cd "$REPO_ROOT" && { git diff --name-only -- tests/ 2>/dev/null; git diff --name-only --cached -- tests/ 2>/dev/null; } | head -1)

if [ -z "$TEST_CHANGES" ]; then
  echo '{"decision":"block","reason":"TDD violation: tests/ has no changes yet. Write failing tests BEFORE editing src/ or include/."}'
  exit 0
fi

exit 0
