#!/usr/bin/env bash
#
# Usage:
#   ./scripts/git_sync.sh [--push] "commit message"
#
# Parameters:
#   --push, -p     Pull, stage, commit, and push to the current branch's upstream.
#   --help, -h     Display this usage information and exit.
#   commit message Required non-empty commit message for the new commit.
#
# Examples:
#   ./scripts/git_sync.sh "整理客户端目录"
#   ./scripts/git_sync.sh --push "整理客户端目录并添加开发规则"
#
# Run example:
#   ./scripts/git_sync.sh --push "整理客户端目录并添加开发规则"
# Command description:
#   Pull the current branch with rebase and autostash, stage all repository changes,
#   create one commit, and optionally push it to the configured upstream.

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO_ROOT=$(cd "${SCRIPT_DIR}/.." && pwd)

usage() {
  cat <<'EOF'
Usage:
  scripts/git_sync.sh [--push] "commit message"

Options:
  -p, --push       Push the commit to the current branch's upstream after committing.
  -h, --help       Show this help message.

Behavior:
  1. Verify that the repository is on a branch with an upstream.
  2. Pull upstream changes with --rebase --autostash.
  3. Stage all changes with git add --all, including new, deleted, and moved files.
  4. Create one commit using the supplied commit message.
  5. Push only when --push is provided.

Notes:
  - Run this script from the repository root or any subdirectory.
  - The script stops on pull conflicts, merge/rebase-in-progress states, or commit errors.
  - Review the staged file list printed before the commit when moving or deleting files.
EOF
}

push_after_commit=false
commit_message=""

while [ "$#" -gt 0 ]; do
  case "$1" in
    -p|--push)
      push_after_commit=true
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    --)
      shift
      if [ "$#" -ne 1 ]; then
        echo "expected exactly one commit message after --" >&2
        usage >&2
        exit 2
      fi
      commit_message=$1
      shift
      ;;
    -* )
      echo "unknown option: $1" >&2
      usage >&2
      exit 2
      ;;
    *)
      if [ -n "${commit_message}" ]; then
        echo "expected exactly one commit message" >&2
        usage >&2
        exit 2
      fi
      commit_message=$1
      shift
      ;;
  esac
done

if [ -z "${commit_message}" ]; then
  echo "a non-empty commit message is required" >&2
  usage >&2
  exit 2
fi

cd "${REPO_ROOT}"

if ! git rev-parse --show-toplevel >/dev/null 2>&1; then
  echo "not a Git repository: ${REPO_ROOT}" >&2
  exit 2
fi

if ! branch=$(git symbolic-ref --quiet --short HEAD); then
  echo "detached HEAD is not supported; check out a branch first" >&2
  exit 2
fi

if git rev-parse --verify --quiet MERGE_HEAD >/dev/null || \
   git rev-parse --verify --quiet CHERRY_PICK_HEAD >/dev/null || \
   git rev-parse --verify --quiet REVERT_HEAD >/dev/null || \
   [ -d "$(git rev-parse --git-path rebase-merge)" ] || \
   [ -d "$(git rev-parse --git-path rebase-apply)" ]; then
  echo "another Git merge, rebase, cherry-pick, or revert is already in progress" >&2
  exit 2
fi

if ! upstream=$(git rev-parse --abbrev-ref --symbolic-full-name '@{upstream}' 2>/dev/null); then
  echo "branch '${branch}' has no upstream; configure one before using this script" >&2
  echo "example: git push -u origin ${branch}" >&2
  exit 2
fi

printf 'Branch:   %s\n' "${branch}"
printf 'Upstream: %s\n' "${upstream}"
printf 'Pulling latest upstream changes...\n'
if ! git pull --rebase --autostash; then
  echo "pull failed; resolve the reported conflict or network error before retrying" >&2
  exit 1
fi

unmerged_files=$(git ls-files --unmerged)
if [ -n "${unmerged_files}" ]; then
  echo "unmerged files remain after pull; commit was not created" >&2
  printf '%s\n' "${unmerged_files}" >&2
  exit 1
fi

printf 'Staging all repository changes...\n'
git add --all

if git diff --cached --quiet; then
  echo "no changes to commit"
  if [ "${push_after_commit}" = true ]; then
    printf 'Pushing existing local commits...\n'
    git push
  fi
  exit 0
fi

printf '\nFiles staged for commit:\n'
git diff --cached --name-status --find-renames
printf '\nStaged change summary:\n'
git diff --cached --stat

printf '\nCreating commit...\n'
git commit -m "${commit_message}"

if [ "${push_after_commit}" = true ]; then
  printf 'Pushing commit to %s...\n' "${upstream}"
  git push
else
  printf 'Commit created locally. Use --push or run git push to upload it.\n'
fi

printf '\nFinal status:\n'
git status --short
