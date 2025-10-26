#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
ARTIFACT_DIR="${REPO_ROOT}/out"
ARTIFACT_FILE="${ARTIFACT_DIR}/build-info.txt"

rm -rf "${ARTIFACT_DIR}"
mkdir -p "${ARTIFACT_DIR}"

git_head="$(git -C "${REPO_ROOT}" rev-parse HEAD)"
git_subject="$(git -C "${REPO_ROOT}" log -1 --pretty=%s)"
git_status="$(git -C "${REPO_ROOT}" status --short || true)"

{
  echo "CubieKernel build summary"
  echo "=========================="
  echo "Commit: ${git_head}"
  echo "Subject: ${git_subject}"
  echo "Timestamp: $(date -u +"%Y-%m-%dT%H:%M:%SZ")"
  echo
  echo "Repository status:"
  if [[ -z "${git_status}" ]]; then
    echo "  clean"
  else
    while IFS= read -r line; do
      echo "  ${line}"
    done <<< "${git_status}"
  fi
} > "${ARTIFACT_FILE}"

printf 'Build artefact created at %s\n' "${ARTIFACT_FILE}"
