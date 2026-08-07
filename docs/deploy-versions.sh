#!/usr/bin/env bash
# Build a multi-version docs tree for GitHub Pages (artifact deploy).
#
# Versions:
#   latest  — tip of main
#   X.Y     — newest X.Y.Z tag in that minor line (X.Y >= 0.17)
#
# Uses mike locally (no push), then exports the branch into ./site for
# actions/upload-pages-artifact.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$SCRIPT_DIR/.." && pwd)"
MIKE_BRANCH="${MIKE_BRANCH:-docs-versions}"
WORK_ROOT="${TMPDIR:-/tmp}/esp-now-midi-docs-$$"
MIN_MAJOR=0
MIN_MINOR=17

cd "$REPO"

if [ -x "$REPO/.venv/bin/mike" ]; then
  MIKE="$REPO/.venv/bin/mike"
  # mike invokes `mkdocs` from PATH; keep the venv first for local runs.
  export PATH="$REPO/.venv/bin:$PATH"
elif command -v mike >/dev/null 2>&1; then
  MIKE="mike"
else
  echo "error: mike not found. Install with: pip install -r docs/requirements.txt" >&2
  exit 1
fi

# Prefer env identity so we never write to .git/config (mike needs author/committer).
export GIT_AUTHOR_NAME="${GIT_AUTHOR_NAME:-github-actions[bot]}"
export GIT_AUTHOR_EMAIL="${GIT_AUTHOR_EMAIL:-github-actions[bot]@users.noreply.github.com}"
export GIT_COMMITTER_NAME="${GIT_COMMITTER_NAME:-$GIT_AUTHOR_NAME}"
export GIT_COMMITTER_EMAIL="${GIT_COMMITTER_EMAIL:-$GIT_AUTHOR_EMAIL}"

cleanup() {
  if [ -d "$WORK_ROOT" ]; then
    # Remove worktrees first so the directory can be deleted.
    find "$WORK_ROOT" -mindepth 1 -maxdepth 1 -type d 2>/dev/null | while read -r wt; do
      git worktree remove --force "$wt" 2>/dev/null || rm -rf "$wt"
    done
    rm -rf "$WORK_ROOT"
  fi
  git worktree prune 2>/dev/null || true
}
trap cleanup EXIT

mkdir -p "$WORK_ROOT"

# Drop any previous local mike branch so this run is self-contained.
if git show-ref --verify --quiet "refs/heads/$MIKE_BRANCH"; then
  git branch -D "$MIKE_BRANCH" >/dev/null
fi

ensure_mike_provider() {
  local cfg="$1/mkdocs.yml"
  if [ ! -f "$cfg" ]; then
    return 1
  fi
  if ! grep -qE 'provider:[[:space:]]*mike' "$cfg"; then
    printf '\nextra:\n  version:\n    provider: mike\n' >> "$cfg"
  fi
}

prepare_and_deploy() {
  local version_id="$1"
  local source_dir="$2"

  echo "==> Version $version_id from $source_dir"

  if [ ! -f "$source_dir/mkdocs.yml" ] || [ ! -f "$source_dir/Doxyfile" ]; then
    echo "    skip: missing mkdocs.yml or Doxyfile"
    return 0
  fi

  ensure_mike_provider "$source_dir"

  # Keep header CSS in sync across tagged minors (version selector layout).
  if [ -f "$SCRIPT_DIR/extra.css" ] && [ -d "$source_dir/docs" ] \
      && [ ! "$SCRIPT_DIR/extra.css" -ef "$source_dir/docs/extra.css" ]; then
    cp "$SCRIPT_DIR/extra.css" "$source_dir/docs/extra.css"
  fi

  if ! DOCS_ROOT="$source_dir" DOCS_VERSION="$version_id" DOCS_TOOLS_DIR="$REPO/.tools" \
      bash "$SCRIPT_DIR/generate.sh" prepare; then
    echo "    skip: generate prepare failed for $version_id" >&2
    return 0
  fi

  # mike deploy runs mkdocs build from the source tree.
  if ! (cd "$source_dir" && "$MIKE" deploy "$version_id" -b "$MIKE_BRANCH" --ignore-remote-status); then
    echo "    skip: mike deploy failed for $version_id" >&2
    return 0
  fi
}

# --- latest ------------------------------------------------------------------
# Default: tip of main. Override with DOCS_LATEST_ROOT=. to use the working tree
# (useful when testing uncommitted docs CSS/content locally).

if [ -n "${DOCS_LATEST_ROOT:-}" ]; then
  prepare_and_deploy "latest" "$(cd "$DOCS_LATEST_ROOT" && pwd)"
else
  git fetch origin main --quiet 2>/dev/null || true
  if git show-ref --verify --quiet refs/remotes/origin/main; then
    MAIN_REF="origin/main"
  elif git show-ref --verify --quiet refs/heads/main; then
    MAIN_REF="main"
  else
    echo "error: cannot resolve main branch (or set DOCS_LATEST_ROOT)" >&2
    exit 1
  fi

  LATEST_DIR="$WORK_ROOT/latest"
  git worktree add --detach "$LATEST_DIR" "$MAIN_REF" >/dev/null
  prepare_and_deploy "latest" "$LATEST_DIR"
fi

# --- one dir per major.minor (newest patch) ---------------------------------

MINOR_MAP="$(mktemp)"
{
  git tag -l | sed 's/^v//' | grep -E '^[0-9]+\.[0-9]+\.[0-9]+$' | sort -V | while read -r ver; do
    major="${ver%%.*}"
    rest="${ver#*.}"
    minor="${rest%%.*}"
    if [ "$major" -lt "$MIN_MAJOR" ]; then
      continue
    fi
    if [ "$major" -eq "$MIN_MAJOR" ] && [ "$minor" -lt "$MIN_MINOR" ]; then
      continue
    fi
    if git rev-parse -q --verify "refs/tags/$ver" >/dev/null; then
      echo "$major.$minor $ver"
    elif git rev-parse -q --verify "refs/tags/v$ver" >/dev/null; then
      echo "$major.$minor v$ver"
    fi
  done
} | awk '{ latest[$1] = $2 } END { for (m in latest) print m, latest[m] }' | sort -V > "$MINOR_MAP"

while read -r minor_id tag; do
  [ -n "${minor_id:-}" ] || continue
  wt="$WORK_ROOT/v-$minor_id"
  if ! git worktree add --detach "$wt" "refs/tags/$tag" >/dev/null 2>&1; then
    echo "==> Version $minor_id: skip (cannot checkout tag $tag)" >&2
    continue
  fi
  prepare_and_deploy "$minor_id" "$wt"
done < "$MINOR_MAP"
rm -f "$MINOR_MAP"

if ! git show-ref --verify --quiet "refs/heads/$MIKE_BRANCH"; then
  echo "error: no versions were deployed" >&2
  exit 1
fi

"$MIKE" set-default latest -b "$MIKE_BRANCH" --ignore-remote-status

echo "==> Exporting $MIKE_BRANCH to $REPO/site"
rm -rf "$REPO/site"
mkdir -p "$REPO/site"
git archive "$MIKE_BRANCH" | tar -x -C "$REPO/site"
git branch -D "$MIKE_BRANCH" >/dev/null

echo "==> Versions ready:"
if [ -f "$REPO/site/versions.json" ]; then
  cat "$REPO/site/versions.json"
fi
