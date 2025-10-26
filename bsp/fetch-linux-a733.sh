#!/bin/sh
set -eu

if [ "${GIT:-}" = "" ]; then
    GIT=git
fi

if ! command -v "$GIT" >/dev/null 2>&1; then
    echo "error: git not found in PATH" >&2
    exit 1
fi

BSP_DIR=$(dirname "$0")/linux-a733

if [ -f "$BSP_DIR/.placeholder" ]; then
    echo "Removing placeholder BSP directory" >&2
    rm -rf "$BSP_DIR"
fi

if [ -d "$BSP_DIR/.git" ]; then
    echo "Updating existing linux-a733 BSP repository" >&2
    exec "$GIT" -C "$BSP_DIR" pull --ff-only
fi

if [ -e "$BSP_DIR" ]; then
    echo "error: $BSP_DIR already exists but is not a git checkout" >&2
    exit 1
fi

REPO_URL=${REPO_URL:-https://github.com/radxa-pkg/linux-a733}

exec "$GIT" clone --depth=1 "$REPO_URL" "$BSP_DIR"
