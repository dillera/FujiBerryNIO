#!/usr/bin/env bash
# Clone (or update) fujinet-nio-workspace with its submodules, then apply the
# portability patches in patches/.
#
# Re-running is safe: an existing clone is fast-forwarded, submodules are
# moved to the commits the workspace pins, and a patch is skipped when it is
# already applied (or has since been merged upstream).
#
# If an upstream update moves a submodule whose files these patches touched,
# the checkout stops rather than discarding them: `git -C workspace/repos/<x>
# checkout .` the patched files, then re-run to re-apply on the new commit.
set -euo pipefail

PROJ="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
WS="${WS:-$PROJ/workspace}"
REMOTE="${REMOTE:-https://github.com/markjfisher/fujinet-nio-workspace}"

if [ -d "$WS/.git" ]; then
  echo "==> updating $WS"
  git -C "$WS" pull --ff-only
else
  echo "==> cloning $REMOTE -> $WS"
  git clone "$REMOTE" "$WS"
fi
echo "==> syncing submodules (first run takes a while)"
git -C "$WS" submodule update --init --recursive

# patch file -> repository it applies to, relative to the workspace.
PATCHES=(
  "0001-fujinet-nio-getentropy.patch:repos/fujinet-nio"
  "0002-workspace-harness-fixes.patch:."
  "0003-driver-darwin-mkdtemp.patch:repos/fujinet-nio-driver"
  "0004-driver-nio-dosbase-null.patch:repos/fujinet-nio-driver"
)

for entry in "${PATCHES[@]}"; do
  patch="$PROJ/patches/${entry%%:*}"
  repo="$WS/${entry#*:}"
  if git -C "$repo" apply --check "$patch" 2>/dev/null; then
    git -C "$repo" apply "$patch"
    echo "==> applied  ${entry%%:*}"
  elif git -C "$repo" apply --check -R "$patch" 2>/dev/null; then
    echo "==> present  ${entry%%:*} (already applied or merged upstream)"
  else
    echo "==> CONFLICT ${entry%%:*}: upstream changed the same lines; review by hand" >&2
    exit 1
  fi
done
