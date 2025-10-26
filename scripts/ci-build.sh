#!/usr/bin/env bash
set -euo pipefail

log() {
  printf '[%s] %s\n' "$(date -u +'%Y-%m-%dT%H:%M:%SZ')" "$*"
}

die() {
  log "ERROR: $*"
  exit 1
}

trap 'status=$?; if (( status != 0 )); then log "Build failed with exit status ${status}"; fi' EXIT

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
ARTIFACT_DIR="${REPO_ROOT}/out"
ARTIFACT_FILE="${ARTIFACT_DIR}/build-info.txt"
BUILD_DIR="${ARTIFACT_DIR}/build"
MODULES_DIR="${ARTIFACT_DIR}/modules"
IMAGE_DIR="${ARTIFACT_DIR}/images"

ARCH="${ARCH:-arm}"
CROSS_COMPILE="${CROSS_COMPILE:-arm-linux-gnueabihf-}"
DEFCONFIG="${DEFCONFIG:-sunxi_defconfig}"
DTB_PATTERN="${DTB_PATTERN:-sun8i-a33*.dtb}"
JOBS="${JOBS:-$(nproc)}"

log "Starting CubieKernel build"
log "Repository root: ${REPO_ROOT}"
log "Host: $(uname -a)"
log "Using ARCH=${ARCH}"
log "Using CROSS_COMPILE=${CROSS_COMPILE}"
log "Using DEFCONFIG=${DEFCONFIG}"
log "Using JOBS=${JOBS}"

if ! command -v git >/dev/null 2>&1; then
  die "git is required but not found in PATH"
fi

if ! command -v make >/dev/null 2>&1; then
  die "make is required but not found in PATH"
fi

if ! command -v "${CROSS_COMPILE}gcc" >/dev/null 2>&1; then
  die "Cross-compiler ${CROSS_COMPILE}gcc not found. Install an ARM toolchain or adjust CROSS_COMPILE"
fi

log "make version: $(make --version | head -n 1)"
log "cross-compiler version: $(${CROSS_COMPILE}gcc --version | head -n 1)"
log "Available CPU cores: $(nproc)"
log "Free disk space: $(df -h "${REPO_ROOT}" | tail -n 1)"

rm -rf "${ARTIFACT_DIR}"
mkdir -p "${ARTIFACT_DIR}" "${BUILD_DIR}" "${MODULES_DIR}" "${IMAGE_DIR}"

if [[ -d "${REPO_ROOT}/bsp/linux-a733/.git" ]]; then
  bsp_head=$(git -C "${REPO_ROOT}/bsp/linux-a733" rev-parse --short HEAD)
  log "Detected linux-a733 BSP checkout at commit ${bsp_head}"
else
  log "Warning: linux-a733 BSP repository not populated; using placeholder configuration"
fi

log "Generating ${DEFCONFIG}"
make -C "${REPO_ROOT}" O="${BUILD_DIR}" ARCH="${ARCH}" CROSS_COMPILE="${CROSS_COMPILE}" "${DEFCONFIG}"

log "Building kernel image, modules, and device trees"
make -C "${REPO_ROOT}" \
     O="${BUILD_DIR}" \
     ARCH="${ARCH}" \
     CROSS_COMPILE="${CROSS_COMPILE}" \
     -j"${JOBS}" \
     zImage modules dtbs

log "Installing kernel modules"
make -C "${REPO_ROOT}" \
     O="${BUILD_DIR}" \
     ARCH="${ARCH}" \
     CROSS_COMPILE="${CROSS_COMPILE}" \
     INSTALL_MOD_PATH="${MODULES_DIR}" \
     modules_install

kernel_release=$(make -s -C "${REPO_ROOT}" O="${BUILD_DIR}" ARCH="${ARCH}" CROSS_COMPILE="${CROSS_COMPILE}" kernelrelease)
log "Kernel release: ${kernel_release}"

zimage_path="${BUILD_DIR}/arch/${ARCH}/boot/zImage"
if [[ -f "${zimage_path}" ]]; then
  cp "${zimage_path}" "${IMAGE_DIR}/zImage"
  log "Copied kernel image to ${IMAGE_DIR}/zImage"
else
  die "Expected kernel image ${zimage_path} not found"
fi

dtb_dir="${BUILD_DIR}/arch/${ARCH}/boot/dts"
shopt -s nullglob
dtb_files=("${dtb_dir}"/${DTB_PATTERN})
shopt -u nullglob

if ((${#dtb_files[@]} == 0)); then
  log "Warning: no DTBs matching pattern '${DTB_PATTERN}' were produced"
else
  for dtb in "${dtb_files[@]}"; do
    cp "${dtb}" "${IMAGE_DIR}/"
    log "Collected device tree $(basename "${dtb}")"
  done
fi

checksum_file="${ARTIFACT_DIR}/checksums.txt"
if compgen -G "${IMAGE_DIR}/*" >/dev/null; then
  (cd "${IMAGE_DIR}" && sha256sum * ) > "${checksum_file}"
  log "Recorded image checksums in ${checksum_file}"
else
  log "Warning: no images collected for checksum generation"
fi

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
  echo
  echo "Build parameters:"
  echo "  ARCH=${ARCH}"
  echo "  CROSS_COMPILE=${CROSS_COMPILE}"
  echo "  DEFCONFIG=${DEFCONFIG}"
  echo "  JOBS=${JOBS}"
  echo "  Kernel release: ${kernel_release}"
  echo
  if [[ -s "${checksum_file}" ]]; then
    echo "Images (sha256):"
    while IFS= read -r line; do
      echo "  ${line}"
    done < "${checksum_file}"
    echo
  fi
  echo "Modules installed under: ${MODULES_DIR}"
} > "${ARTIFACT_FILE}"

log "Build artefacts available in ${ARTIFACT_DIR}"
log "Build summary written to ${ARTIFACT_FILE}"
