#!/usr/bin/env bash
#
# Usage:
#   ./scripts/git_sync.sh [--push] "commit message"
#   ./scripts/git_sync.sh --revert [--yes] [path ...]
#   ./scripts/git_sync.sh --switch branch
#   ./scripts/git_sync.sh --switch-back
#
# Parameters:
#   --push, -p     Pull, stage, commit, and push to the current branch's upstream.
#   --revert, -r   Discard uncommitted tracked changes, similar to svn revert.
#   --yes, -y      Skip the confirmation prompt for --revert.
#   --switch        Save local changes in a stash and switch to an existing local branch.
#   --switch-back   Return to the branch saved by --switch and restore the local changes.
#   --help, -h     Display this usage information and exit.
#   commit message Required non-empty commit message for the commit operation.
#   path           Optional file or directory for --revert; defaults to the whole repository.
#
# Examples:
#   ./scripts/git_sync.sh "整理客户端目录"
#   ./scripts/git_sync.sh --push "整理客户端目录并添加开发规则"
#   ./scripts/git_sync.sh --revert client/docs
#   ./scripts/git_sync.sh --revert --yes
#   ./scripts/git_sync.sh --switch feature/login
#   ./scripts/git_sync.sh --switch-back
#
# Run example:
#   ./scripts/git_sync.sh --push "整理客户端目录并添加开发规则"
# Command description:
#   Pull the current branch with rebase and autostash, stage all repository changes,
#   create one commit, and optionally push it to the configured upstream; it can
#   also discard uncommitted tracked changes without touching untracked files, or
#   save local changes while temporarily working on another branch.

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO_ROOT=$(cd "${SCRIPT_DIR}/.." && pwd)

usage() {
  cat <<'EOF'
Usage:
  scripts/git_sync.sh [--push] "commit message"
  scripts/git_sync.sh --revert [--yes] [path ...]
  scripts/git_sync.sh --switch branch
  scripts/git_sync.sh --switch-back

Options:
  -p, --push       Push the commit to the current branch's upstream after committing.
  -r, --revert     Discard uncommitted tracked changes, similar to svn revert.
  -y, --yes        Skip the confirmation prompt for --revert.
      --switch     Save local changes and switch to an existing local branch.
      --switch-back Return to the saved branch and restore the saved changes.
  -h, --help       Show this help message.

Commit behavior:
  1. Verify that the repository is on a branch with an upstream.
  2. Pull upstream changes with --rebase --autostash.
  3. Stage all changes with git add --all, including new, deleted, and moved files.
  4. Create one commit using the supplied commit message.
  5. Push only when --push is provided.

Revert behavior:
  - Restores tracked files to HEAD, including both staged and unstaged changes.
  - With paths, restores only the specified files or directories.
  - Without paths, restores all tracked changes in the repository.
  - Does not remove untracked or ignored files.
  - Asks for confirmation unless --yes is provided.

Switch behavior:
  - --switch saves tracked and untracked changes with git stash.
  - --switch requires an existing local branch and records the original branch.
  - --switch-back returns to the original branch and restores the saved stash.
  - Changes made on the temporary branch must be committed or stashed first.
  - The switch state is stored in the local repository Git config.

Notes:
  - Run this script from the repository root or any subdirectory.
  - The script stops on pull conflicts, merge/rebase-in-progress states, or Git errors.
  - Review the staged file list printed before the commit when moving or deleting files.
EOF
}

operation=commit
push_after_commit=false
revert_confirmed=false
commit_message=""
revert_paths=()
switch_target=""

while [ "$#" -gt 0 ]; do
  case "$1" in
    -p|--push)
      push_after_commit=true
      shift
      ;;
    -r|--revert)
      if [ "${operation}" != commit ] && [ "${operation}" != revert ]; then
        echo "--revert cannot be combined with another operation" >&2
        exit 2
      fi
      operation=revert
      shift
      ;;
    --switch)
      if [ "${operation}" != commit ]; then
        echo "--switch cannot be combined with another operation" >&2
        exit 2
      fi
      shift
      if [ "$#" -eq 0 ] || [[ "$1" == -* ]]; then
        echo "--switch requires a branch name" >&2
        usage >&2
        exit 2
      fi
      operation=switch
      switch_target=$1
      shift
      ;;
    --switch-back)
      if [ "${operation}" != commit ]; then
        echo "--switch-back cannot be combined with another operation" >&2
        exit 2
      fi
      operation=switch-back
      shift
      ;;
    -y|--yes)
      revert_confirmed=true
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    --)
      shift
      if [ "${operation}" = revert ]; then
        revert_paths+=("$@")
        shift "$#"
      elif [ "${operation}" = switch ] || [ "${operation}" = switch-back ]; then
        echo "${operation} does not accept arguments after --" >&2
        usage >&2
        exit 2
      else
        if [ "$#" -ne 1 ]; then
          echo "expected exactly one commit message after --" >&2
          usage >&2
          exit 2
        fi
        commit_message=$1
        shift
      fi
      ;;
    -* )
      echo "unknown option: $1" >&2
      usage >&2
      exit 2
      ;;
    *)
      if [ "${operation}" = revert ]; then
        revert_paths+=("$1")
        shift
      elif [ "${operation}" = switch ] || [ "${operation}" = switch-back ]; then
        echo "${operation} does not accept extra arguments: $1" >&2
        usage >&2
        exit 2
      else
        if [ -n "${commit_message}" ]; then
          echo "expected exactly one commit message" >&2
          usage >&2
          exit 2
        fi
        commit_message=$1
        shift
      fi
      ;;
  esac
done

if [ "${operation}" = revert ] && [ "${push_after_commit}" = true ]; then
  echo "--push cannot be combined with --revert" >&2
  usage >&2
  exit 2
fi

if [ "${operation}" = switch ] || [ "${operation}" = switch-back ]; then
  if [ "${push_after_commit}" = true ]; then
    echo "--push cannot be combined with ${operation}" >&2
    usage >&2
    exit 2
  fi
fi

if [ "${operation}" = commit ] && [ "${revert_confirmed}" = true ]; then
  echo "--yes can only be used with --revert" >&2
  usage >&2
  exit 2
fi

if [ "${operation}" = commit ] && [ -z "${commit_message}" ]; then
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
  branch="(detached HEAD)"
fi

if git rev-parse --verify --quiet MERGE_HEAD >/dev/null || \
   git rev-parse --verify --quiet CHERRY_PICK_HEAD >/dev/null || \
   git rev-parse --verify --quiet REVERT_HEAD >/dev/null || \
   [ -d "$(git rev-parse --git-path rebase-merge)" ] || \
   [ -d "$(git rev-parse --git-path rebase-apply)" ]; then
  echo "another Git merge, rebase, cherry-pick, or revert is already in progress" >&2
  exit 2
fi

switch_state_prefix=git-sync.switch

switch_state_value() {
  local key

  key=$1
  git config --local --get "${switch_state_prefix}.${key}" 2>/dev/null || true
}

clear_switch_state() {
  git config --local --unset-all "${switch_state_prefix}.original-branch" 2>/dev/null || true
  git config --local --unset-all "${switch_state_prefix}.target-branch" 2>/dev/null || true
  git config --local --unset-all "${switch_state_prefix}.stash-ref" 2>/dev/null || true
  git config --local --unset-all "${switch_state_prefix}.stash-message" 2>/dev/null || true
}

if [ "${operation}" = switch ]; then
  if [ "${branch}" = "(detached HEAD)" ]; then
    echo "detached HEAD is not supported for --switch; check out a branch first" >&2
    exit 2
  fi

  if [ "${switch_target}" = "${branch}" ]; then
    echo "target branch is already the current branch: ${branch}" >&2
    exit 2
  fi

  if ! git check-ref-format --branch "${switch_target}" >/dev/null 2>&1 || \
     ! git show-ref --verify --quiet "refs/heads/${switch_target}"; then
    echo "local branch not found: ${switch_target}" >&2
    echo "create or check out the branch first with: git switch -c ${switch_target}" >&2
    exit 2
  fi

  if [ -n "$(switch_state_value original-branch)" ]; then
    echo "a saved branch switch already exists; run --switch-back first" >&2
    exit 2
  fi

  stash_ref=none
  stash_message=none
  local_status=$(git status --short --untracked-files=all)
  if [ -n "${local_status}" ]; then
    stash_message="git-sync switch ${branch} to ${switch_target} $(date '+%Y%m%d%H%M%S')"
    printf 'Saving local changes in a stash...\n'
    git stash push --include-untracked -m "${stash_message}"
    stash_ref=$(git stash list --format='%gd%x09%gs' | \
      awk -F '\t' -v message="${stash_message}" 'index($0, message) { print $1; exit }')
    if [ -z "${stash_ref}" ]; then
      echo "could not locate the newly created stash; branch switch was cancelled" >&2
      exit 1
    fi
  fi

  git config --local "${switch_state_prefix}.original-branch" "${branch}"
  git config --local "${switch_state_prefix}.target-branch" "${switch_target}"
  git config --local "${switch_state_prefix}.stash-ref" "${stash_ref}"
  git config --local "${switch_state_prefix}.stash-message" "${stash_message}"

  if ! git switch "${switch_target}"; then
    echo "branch switch failed; restoring the saved local changes" >&2
    if [ "${stash_ref}" != none ] && ! git stash pop --index "${stash_ref}"; then
      echo "could not automatically restore the stash: ${stash_ref}" >&2
      echo "restore it manually with: git stash pop --index ${stash_ref}" >&2
      exit 1
    fi
    clear_switch_state
    exit 1
  fi

  printf 'Switched from %s to %s.\n' "${branch}" "${switch_target}"
  if [ "${stash_ref}" != none ]; then
    printf 'Local changes saved as %s. Run --switch-back to restore them.\n' "${stash_ref}"
  else
    echo "There were no local changes to save. Run --switch-back to return later."
  fi
  exit 0
fi

if [ "${operation}" = switch-back ]; then
  original_branch=$(switch_state_value original-branch)
  saved_target=$(switch_state_value target-branch)
  stash_ref=$(switch_state_value stash-ref)

  if [ -z "${original_branch}" ] || [ -z "${saved_target}" ] || [ -z "${stash_ref}" ]; then
    echo "no saved branch switch found; run --switch branch first" >&2
    exit 2
  fi

  if [ "${branch}" != "${saved_target}" ] && [ "${branch}" != "${original_branch}" ]; then
    echo "current branch '${branch}' is neither saved target '${saved_target}' nor original '${original_branch}'" >&2
    exit 2
  fi

  if [ "${branch}" = "${saved_target}" ]; then
    if [ -n "$(git status --short --untracked-files=all)" ]; then
      echo "current branch '${saved_target}' has uncommitted changes" >&2
      echo "commit or stash temporary work before running --switch-back" >&2
      exit 1
    fi

    printf 'Switching back to %s...\n' "${original_branch}"
    if ! git switch "${original_branch}"; then
      echo "could not switch back; commit or stash changes on '${saved_target}' first" >&2
      exit 1
    fi
  fi

  if [ "${stash_ref}" != none ]; then
    stash_message=$(switch_state_value stash-message)
    if [ -z "${stash_message}" ] || [ "${stash_message}" = none ]; then
      echo "saved stash metadata is incomplete; restore it manually before continuing" >&2
      exit 1
    fi

    stash_ref=$(git stash list --format='%gd%x09%gs' | \
      awk -F '\t' -v message="${stash_message}" 'index($0, message) { print $1; exit }')
    if [ -z "${stash_ref}" ]; then
      echo "saved stash was not found: ${stash_message}" >&2
      echo "inspect available stashes with: git stash list" >&2
      exit 1
    fi

    printf 'Restoring local changes from %s...\n' "${stash_ref}"
    if ! git stash pop --index "${stash_ref}"; then
      echo "stash restore has conflicts; the stash was kept as ${stash_ref}" >&2
      echo "resolve the conflicts manually, then drop the stash when finished" >&2
      exit 1
    fi
  fi

  clear_switch_state
  printf 'Returned to %s and restored the previous local changes.\n' "${original_branch}"
  git status --short
  exit 0
fi

if [ "${operation}" = revert ]; then
  if ! tracked_status=$(git status --short --untracked-files=no -- "${revert_paths[@]}" 2>&1); then
    printf 'unable to inspect revert path(s):\n%s\n' "${tracked_status}" >&2
    exit 2
  fi

  if [ -z "${tracked_status}" ]; then
    echo "no tracked changes to revert"
    if [ -n "$(git status --short --untracked-files=all)" ]; then
      echo "untracked or ignored files were left unchanged"
    fi
    exit 0
  fi

  printf 'Tracked changes to revert:\n%s\n' "${tracked_status}"
  if [ "${revert_confirmed}" = false ]; then
    if [ ! -t 0 ]; then
      echo "confirmation is required in non-interactive mode; use --yes to continue" >&2
      exit 2
    fi

    if ! read -r -p "Discard these tracked changes? [y/N] " answer; then
      echo "revert cancelled" >&2
      exit 2
    fi
    case "${answer}" in
      y|Y|yes|YES|Yes)
        ;;
      *)
        echo "revert cancelled"
        exit 0
        ;;
    esac
  fi

  printf 'Reverting tracked changes to HEAD...\n'
  if [ "${#revert_paths[@]}" -eq 0 ]; then
    git restore --source=HEAD --staged --worktree -- .
  else
    git restore --source=HEAD --staged --worktree -- "${revert_paths[@]}"
  fi

  echo "revert completed; untracked and ignored files were not removed"
  git status --short
  exit 0
fi

if [ "${branch}" = "(detached HEAD)" ]; then
  echo "detached HEAD is not supported for commit; check out a branch first" >&2
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
