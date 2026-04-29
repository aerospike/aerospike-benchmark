#!/usr/bin/env bash
#
# docker/docker-build.sh
#
# Build multi-architecture Docker images for Aerospike asbench.
#
# Flow:
#   1. Call ./update-version.sh to patch Dockerfile ARGs (URLs + SHA256)
#   2. Generate docker-bake.hcl with test and push targets
#   3. Build with docker buildx bake
#
# Modes:
#   -t   Test: build and load locally (one image per arch, --load compatible)
#   -p   Push: build and push to registry (multi-arch, or single-arch per-tag when -a selects one arch)
#   -M   Manifest: stitch per-arch tags into a multi-arch manifest (buildx imagetools create --push)
#   -g   Update Dockerfiles only (no bake / no build)
#

set -Eeuo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "${SCRIPT_DIR}"

BAKE_FILE="docker-bake.hcl"

# ---------------------------------------------------------------------------
# Logging
# ---------------------------------------------------------------------------
log_info()    { printf '\e[36m[INFO]\e[0m  %s\n' "$*"; }
log_success() { printf '\e[32m[OK]\e[0m    %s\n' "$*"; }
log_warn()    { printf '\e[33m[WARN]\e[0m  %s\n' "$*" >&2; }

# ---------------------------------------------------------------------------
# Usage
# ---------------------------------------------------------------------------
function usage() {
  cat <<'EOF'
Usage: docker/docker-build.sh -t|-p|-M|-g -v VERSION [OPTIONS]

Build Docker images for Aerospike asbench (Ubuntu 24.04).

MODES (one required):
    -t               Test mode: build and load locally (single platform per arch)
    -p               Push mode: build and push to registry
                       • Single arch (-a amd64 or -a arm64): pushes one platform with a per-arch tag
                       • No -a / both arches: builds multi-arch manifest and pushes
    -M, --manifest   Manifest mode: stitch existing per-arch tags into a multi-arch manifest
                       using docker buildx imagetools create --push (no image build)
    -g, --generate   Update Dockerfiles only, no build

REQUIRED:
    -v, --version VERSION   asbench version, e.g. 2.2.5 or 2.2.5-rc1
                            Required unless -S/--skip-update is set.

OPTIONS:
    -r, --registry REG      Registry prefix for image tags (default: aerospike)
                            Repeat for multiple registries: -r reg1 -r reg2
                            Example: -r artifact.aerospike.io/database-docker-dev-local
    -d, --distro DISTRO     Filter distro; repeat for multiple (default: all)
                            Values: ubuntu24.04
    -a, --arch ARCH         Filter arch for test targets; repeat for multiple (default: all)
                            Values: amd64, arm64
                            Push always builds both arches regardless of this flag.
    -u, --packages-url URL_OR_PATH
                            Package source — local dir or JFrog base URL:
                              Local:  -u /path/to/dist  (sets --packages-dir)
                              Remote: -u https://aerospike.jfrog.io/artifactory/database-deb-dev-local
                                      (sets --deb-base-url)
    --packages-dir DIR      Explicit local packages dir (forwarded to update-version.sh)
    --deb-base-url URL      Explicit DEB base URL (forwarded to update-version.sh)
    -T, --timestamp TS      Override the timestamp appended to push tags (default: current UTC time,
                            format YYYYMMDDHHmmSS, e.g. 20260421153000)
    -s, --compute-sha       Download packages to compute SHA256 (forwarded to update-version.sh)
    -S, --skip-update       Skip calling update-version.sh (use current Dockerfile ARGs as-is)
    -n, --no-cache          Disable Docker build cache
    -N, --dry-run           Dry run: forward to update-version.sh; skip Docker steps
    -h, --help              Show this help message

DISTROS AND BASE IMAGES:
    ubuntu24.04    Ubuntu 24.04, installs asbench .deb   (linux/amd64 + linux/arm64)

TAGS PRODUCED:
  Test mode (loaded locally):
    <reg>/aerospike-asbench:<version>-<distro>-<arch>
    e.g. aerospike/aerospike-asbench:2.2.5-ubuntu24.04-amd64

  Push mode — single arch (-a amd64 or -a arm64):
    <reg>/aerospike-asbench:<version>-<distro>-<arch>
    e.g. aerospike/aerospike-asbench:2.2.5-ubuntu24.04-amd64

  Push mode — multi-arch (no -a or both -a flags):
    <reg>/aerospike-asbench:<version>
    <reg>/aerospike-asbench:<version>-<timestamp>   (when -T is set)

  Manifest mode (-M):
    Stitches per-arch tags (<version>-<distro>-amd64 + <version>-<distro>-arm64) into:
    <reg>/aerospike-asbench:<version>
    <reg>/aerospike-asbench:<version>-<timestamp>   (when -T is set)

EXAMPLES:
    # Build + load using local packages dir
    docker/docker-build.sh -t -v 2.2.5-rc1 -u /path/to/dist

    # Native per-arch build and local test (from matrix runner)
    docker/docker-build.sh -t -v 2.2.5 -a amd64 -u /path/to/dist

    # Build + push multi-arch to JFrog dev registry
    docker/docker-build.sh -p -v 2.2.5 \
        -r artifact.aerospike.io/database-docker-dev-local \
        -u https://aerospike.jfrog.io/artifactory/database-dev-local

    # Stitch manifest after native per-arch pushes
    docker/docker-build.sh -M -v 2.2.5 -d ubuntu24.04 \
        -r artifact.aerospike.io/database-docker-dev-local

    # Build without updating Dockerfiles
    docker/docker-build.sh -t -v 2.2.5 -S

OUTPUT:
    docker/docker-bake.hcl   Generated bake file (gitignored, not committed)
EOF
}

# ---------------------------------------------------------------------------
# Bake file emitters
# ---------------------------------------------------------------------------

function _emit_test_target() {
  local name="$1" ctx="$2" arch="$3" local_pkg="$4"
  shift 4
  local tags=("$@")
  local n=${#tags[@]}
  echo "target \"${name}\" {"
  echo "  context    = \"${ctx}\""
  echo "  dockerfile = \"Dockerfile\""
  echo "  platforms  = [\"linux/${arch}\"]"
  if [[ -n "${local_pkg}" ]]; then
    echo "  args = { ASBENCH_LOCAL_PKG = \"${local_pkg}\" }"
  fi
  echo "  tags = ["
  for ((i = 0; i < n; i++)); do
    if [[ $i -lt $((n - 1)) ]]; then
      echo "    \"${tags[$i]}\","
    else
      echo "    \"${tags[$i]}\""
    fi
  done
  echo "  ]"
  echo "}"
  echo ""
}

function _emit_single_arch_push_target() {
  local name="$1" ctx="$2" arch="$3" local_pkg="$4"
  shift 4
  local tags=("$@")
  local n=${#tags[@]}
  echo "target \"${name}\" {"
  echo "  context    = \"${ctx}\""
  echo "  dockerfile = \"Dockerfile\""
  echo "  platforms  = [\"linux/${arch}\"]"
  if [[ -n "${local_pkg}" ]]; then
    if [[ "${arch}" == "amd64" ]]; then
      echo "  args = { ASBENCH_LOCAL_PKG_AMD64 = \"${local_pkg}\" }"
    else
      echo "  args = { ASBENCH_LOCAL_PKG_ARM64 = \"${local_pkg}\" }"
    fi
  fi
  echo "  tags = ["
  for ((i = 0; i < n; i++)); do
    if [[ $i -lt $((n - 1)) ]]; then
      echo "    \"${tags[$i]}\","
    else
      echo "    \"${tags[$i]}\""
    fi
  done
  echo "  ]"
  echo "}"
  echo ""
}

function _emit_push_target() {
  local name="$1" ctx="$2" local_pkg_amd64="$3" local_pkg_arm64="$4"
  shift 4
  local tags=("$@")
  local n=${#tags[@]}
  echo "target \"${name}\" {"
  echo "  context    = \"${ctx}\""
  echo "  dockerfile = \"Dockerfile\""
  echo "  platforms  = [\"linux/amd64\", \"linux/arm64\"]"
  if [[ -n "${local_pkg_amd64}" || -n "${local_pkg_arm64}" ]]; then
    echo "  args = {"
    [[ -n "${local_pkg_amd64}" ]] && echo "    ASBENCH_LOCAL_PKG_AMD64 = \"${local_pkg_amd64}\""
    [[ -n "${local_pkg_arm64}" ]] && echo "    ASBENCH_LOCAL_PKG_ARM64 = \"${local_pkg_arm64}\""
    echo "  }"
  fi
  echo "  tags = ["
  for ((i = 0; i < n; i++)); do
    if [[ $i -lt $((n - 1)) ]]; then
      echo "    \"${tags[$i]}\","
    else
      echo "    \"${tags[$i]}\""
    fi
  done
  echo "  ]"
  echo "}"
  echo ""
}

function _emit_group() {
  local name="$1"
  shift
  local targets=("$@")
  local list
  list=$(printf '"%s", ' "${targets[@]}")
  list="${list%, }"
  echo "group \"${name}\" { targets = [${list}] }"
  echo ""
}

# ---------------------------------------------------------------------------
# generate_bake
# ---------------------------------------------------------------------------
function generate_bake() {
  log_info "Generating ${BAKE_FILE}..."

  declare -A distro_ctx=(
    [ubuntu24.04]="ubuntu24.04"
  )

  local test_target_names=()
  local push_target_names=()

  {
    echo "# Generated by docker/docker-build.sh — do not edit by hand."
    echo ""
    echo "variable \"VERSION\" { default = \"${VERSION}\" }"
    echo ""

    for distro in "${ACTIVE_DISTROS[@]}"; do
      local ctx="${distro_ctx[${distro}]}"
      local slug="${distro//\./-}"
      for arch in "${ACTIVE_ARCHES[@]}"; do
        local local_pkg=""
        case "${distro}" in
        ubuntu24.04)
          if [[ "${arch}" == "amd64" ]]; then local_pkg="${LOCAL_PKG_UBUNTU_AMD64}"; fi
          if [[ "${arch}" == "arm64" ]]; then local_pkg="${LOCAL_PKG_UBUNTU_ARM64}"; fi
          ;;
        esac
        local tags=()
        for reg in "${REGISTRY_PREFIXES[@]}"; do
          tags+=("${reg}/aerospike-asbench:${VERSION}-${distro}-${arch}")
        done
        _emit_test_target "${slug}-${arch}" "${ctx}" "${arch}" "${local_pkg}" "${tags[@]}"
        test_target_names+=("${slug}-${arch}")
      done
    done

    local multi_distro=false
    if [[ ${#ACTIVE_DISTROS[@]} -gt 1 ]]; then multi_distro=true; fi

    for distro in "${ACTIVE_DISTROS[@]}"; do
      local ctx="${distro_ctx[${distro}]}"
      local slug="${distro//\./-}"
      if [[ "${PUSH_SINGLE_ARCH}" == true ]]; then
        local arch="${ACTIVE_ARCHES[0]}"
        local local_pkg=""
        case "${distro}" in
        ubuntu24.04)
          if [[ "${arch}" == "amd64" ]]; then local_pkg="${LOCAL_PKG_UBUNTU_AMD64}"; fi
          if [[ "${arch}" == "arm64" ]]; then local_pkg="${LOCAL_PKG_UBUNTU_ARM64}"; fi
          ;;
        esac
        local tags=()
        for reg in "${REGISTRY_PREFIXES[@]}"; do
          tags+=("${reg}/aerospike-asbench:${VERSION}-${distro}-${arch}")
        done
        _emit_single_arch_push_target "${slug}" "${ctx}" "${arch}" "${local_pkg}" "${tags[@]}"
      else
        local push_pkg_amd64="" push_pkg_arm64=""
        case "${distro}" in
        ubuntu24.04)
          push_pkg_amd64="${LOCAL_PKG_UBUNTU_AMD64}"
          push_pkg_arm64="${LOCAL_PKG_UBUNTU_ARM64}"
          ;;
        esac
        local tags=()
        for reg in "${REGISTRY_PREFIXES[@]}"; do
          if [[ "${multi_distro}" == true ]]; then
            tags+=("${reg}/aerospike-asbench:${VERSION}-${distro}")
            if [[ -n "${TIMESTAMP}" ]]; then
              tags+=("${reg}/aerospike-asbench:${VERSION}-${distro}-${TIMESTAMP}")
            fi
          else
            tags+=("${reg}/aerospike-asbench:${VERSION}")
            if [[ -n "${TIMESTAMP}" ]]; then
              tags+=("${reg}/aerospike-asbench:${VERSION}-${TIMESTAMP}")
            fi
          fi
        done
        _emit_push_target "${slug}" "${ctx}" "${push_pkg_amd64}" "${push_pkg_arm64}" "${tags[@]}"
      fi
      push_target_names+=("${slug}")
    done

    if [[ ${#test_target_names[@]} -gt 0 ]]; then _emit_group "test" "${test_target_names[@]}"; fi
    if [[ ${#push_target_names[@]} -gt 0 ]]; then _emit_group "push" "${push_target_names[@]}"; fi

  } >"${BAKE_FILE}"

  log_success "Generated ${BAKE_FILE}"
}

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

VERSION=""
TIMESTAMP="$(date -u +%Y%m%d%H%M%S)"
REGISTRY_PREFIXES=()
ACTIVE_DISTROS=()
ACTIVE_ARCHES=()

LOCAL_PKG_UBUNTU_AMD64=""
LOCAL_PKG_UBUNTU_ARM64=""
LOCAL_PKGS_COPIED=()

PUSH_SINGLE_ARCH=false

function _setup_local_pkgs() {
  local dir="$1"
  local real_dir
  real_dir=$(cd "${dir}" && pwd -P)

  _find_one() {
    find -L "${real_dir}" -maxdepth 3 -name "$1" -type f 2>/dev/null | head -1
  }

  _copy_pkg() {
    local pattern="$1" dest_dir="$2"
    local found
    found=$(_find_one "${pattern}")
    if [[ -n "${found}" ]]; then
      local base
      base="$(basename "${found}")"
      cp "${found}" "${dest_dir}/${base}"
      LOCAL_PKGS_COPIED+=("${dest_dir}/${base}")
      log_info "  Local pkg: ${dest_dir}/${base}" >&2
      printf '%s' "${base}"
    fi
  }

  log_info "Resolving local packages from: ${real_dir}"

  local need_amd64=false need_arm64=false
  for a in "${ACTIVE_ARCHES[@]}"; do
    if [[ "${a}" == "amd64" ]]; then need_amd64=true; fi
    if [[ "${a}" == "arm64" ]]; then need_arm64=true; fi
  done

  for distro in "${ACTIVE_DISTROS[@]}"; do
    case "${distro}" in
    ubuntu24.04)
      if [[ "${need_amd64}" == true ]]; then
        LOCAL_PKG_UBUNTU_AMD64=$(_copy_pkg "aerospike-asbench_*_ubuntu24.04_x86_64.deb" "ubuntu24.04")
      fi
      if [[ "${need_arm64}" == true ]]; then
        LOCAL_PKG_UBUNTU_ARM64=$(_copy_pkg "aerospike-asbench_*_ubuntu24.04_aarch64.deb" "ubuntu24.04")
      fi
      ;;
    esac
  done

  local distros_str="${ACTIVE_DISTROS[*]}"
  if [[ "${need_amd64}" == true && "${distros_str}" == *ubuntu24.04* && -z "${LOCAL_PKG_UBUNTU_AMD64}" ]]; then
    log_warn "DEB (amd64) not found in ${dir} — will fall back to URL"
  fi
  if [[ "${need_arm64}" == true && "${distros_str}" == *ubuntu24.04* && -z "${LOCAL_PKG_UBUNTU_ARM64}" ]]; then
    log_warn "DEB (arm64) not found in ${dir} — will fall back to URL"
  fi
}

function _cleanup_local_pkgs() {
  for f in "${LOCAL_PKGS_COPIED[@]+"${LOCAL_PKGS_COPIED[@]}"}"; do
    if [[ -f "${f}" ]]; then rm -f "${f}"; fi
  done
}

# ---------------------------------------------------------------------------
# run_manifest_mode — stitch per-arch tags into a multi-arch manifest
# ---------------------------------------------------------------------------
function run_manifest_mode() {
  log_info "=== Manifest mode: stitching per-arch tags ==="
  for distro in "${ACTIVE_DISTROS[@]}"; do
    local multi_distro=false
    if [[ ${#ACTIVE_DISTROS[@]} -gt 1 ]]; then multi_distro=true; fi

    local source_tags=()
    for arch in "${ACTIVE_ARCHES[@]}"; do
      for reg in "${REGISTRY_PREFIXES[@]}"; do
        source_tags+=("${reg}/aerospike-asbench:${VERSION}-${distro}-${arch}")
      done
    done

    local dest_tags=()
    for reg in "${REGISTRY_PREFIXES[@]}"; do
      if [[ "${multi_distro}" == true ]]; then
        dest_tags+=("${reg}/aerospike-asbench:${VERSION}-${distro}")
        if [[ -n "${TIMESTAMP}" ]]; then
          dest_tags+=("${reg}/aerospike-asbench:${VERSION}-${distro}-${TIMESTAMP}")
        fi
      else
        dest_tags+=("${reg}/aerospike-asbench:${VERSION}")
        if [[ -n "${TIMESTAMP}" ]]; then
          dest_tags+=("${reg}/aerospike-asbench:${VERSION}-${TIMESTAMP}")
        fi
      fi
    done

    log_info "  Sources : ${source_tags[*]}"
    log_info "  Dest    : ${dest_tags[*]}"

    local tag_args=()
    for t in "${dest_tags[@]}"; do tag_args+=("--tag" "${t}"); done

    docker buildx imagetools create --progress plain \
      "${tag_args[@]}" \
      "${source_tags[@]}"

    log_success "Manifest created for ${distro}."
    log_info "Inspecting manifest..."
    docker buildx imagetools inspect "${dest_tags[0]}"
  done
}

function main() {
  local mode=""
  local skip_update=false
  local dry_run=false
  local no_cache=false
  local full_generate=false
  local generate_only=false
  local distro_filters=()
  local arch_filters=()
  local pkg_url="" packages_dir="" deb_base_url=""
  local compute_sha=false

  while [[ $# -gt 0 ]]; do
    case "$1" in
    -t) mode="test"     ; shift ;;
    -p) mode="push"     ; shift ;;
    -M | --manifest) mode="manifest" ; shift ;;
    -g | --generate)  full_generate=true       ; shift ;;
    -v | --version)       VERSION="$2"             ; shift 2 ;;
    -r | --registry)      REGISTRY_PREFIXES+=("$2") ; shift 2 ;;
    -d | --distro)        distro_filters+=("$2")   ; shift 2 ;;
    -a | --arch)          arch_filters+=("$2")     ; shift 2 ;;
    -u | --packages-url)  pkg_url="$2"             ; shift 2 ;;
    --packages-dir)       packages_dir="$2"        ; shift 2 ;;
    --deb-base-url)       deb_base_url="$2"        ; shift 2 ;;
    -T | --timestamp)     TIMESTAMP="$2"           ; shift 2 ;;
    -s | --compute-sha)   compute_sha=true         ; shift ;;
    -S | --skip-update)   skip_update=true         ; shift ;;
    -n | --no-cache)      no_cache=true            ; shift ;;
    -N | --dry-run)       dry_run=true             ; shift ;;
    -h | --help)      usage ; exit 0 ;;
    *) log_warn "Unknown option: $1" ; usage ; exit 1 ;;
    esac
  done

  if [[ "${full_generate}" == true && -z "${mode}" ]]; then
    generate_only=true
  fi
  if [[ "${full_generate}" == false && -z "${mode}" ]]; then
    log_warn "A mode (-t, -p, -M, or -g) is required."
    usage
    exit 1
  fi

  if [[ -z "${VERSION}" && "${skip_update}" == false && "${mode}" != "manifest" ]]; then
    log_warn "--version is required (or use --skip-update to skip Dockerfile update)."
    usage
    exit 1
  fi

  if [[ ${#REGISTRY_PREFIXES[@]} -eq 0 ]]; then REGISTRY_PREFIXES=("aerospike"); fi

  local all_distros=("ubuntu24.04")
  if [[ ${#distro_filters[@]} -eq 0 ]]; then
    ACTIVE_DISTROS=("${all_distros[@]}")
  else
    for d in "${distro_filters[@]}"; do
      local valid=false
      for ad in "${all_distros[@]}"; do
        if [[ "${ad}" == "${d}" ]]; then ACTIVE_DISTROS+=("${d}"); valid=true; break; fi
      done
      if [[ "${valid}" == false ]]; then log_warn "Unknown distro '${d}' (valid: ${all_distros[*]})"; fi
    done
  fi

  local all_arches=("amd64" "arm64")
  if [[ ${#arch_filters[@]} -eq 0 ]]; then
    ACTIVE_ARCHES=("${all_arches[@]}")
  else
    for a in "${arch_filters[@]}"; do
      case "${a}" in
      amd64 | x86_64)  ACTIVE_ARCHES+=("amd64") ;;
      arm64 | aarch64) ACTIVE_ARCHES+=("arm64") ;;
      *) log_warn "Unknown arch '${a}' (valid: amd64, arm64)" ;;
      esac
    done
  fi

  # Deduplicate ACTIVE_ARCHES (preserve order)
  local seen_arches=()
  local deduped_arches=()
  for a in "${ACTIVE_ARCHES[@]}"; do
    local already=false
    for s in "${seen_arches[@]+"${seen_arches[@]}"}"; do
      if [[ "${s}" == "${a}" ]]; then already=true; break; fi
    done
    if [[ "${already}" == false ]]; then
      deduped_arches+=("${a}")
      seen_arches+=("${a}")
    fi
  done
  ACTIVE_ARCHES=("${deduped_arches[@]+"${deduped_arches[@]}"}")

  if [[ ${#ACTIVE_DISTROS[@]} -eq 0 ]]; then
    log_warn "No valid distros after filtering."
    exit 1
  fi
  if [[ ${#ACTIVE_ARCHES[@]} -eq 0 ]]; then
    log_warn "No valid arches after filtering."
    exit 1
  fi

  # Detect single-arch push mode
  if [[ "${mode}" == "push" && ${#ACTIVE_ARCHES[@]} -eq 1 ]]; then
    PUSH_SINGLE_ARCH=true
    log_info "Single-arch push mode: will push per-arch tagged image (${ACTIVE_ARCHES[0]})"
  fi

  if [[ -z "${VERSION}" && "${skip_update}" == true && "${generate_only}" == false && "${dry_run}" == false ]]; then
    log_warn "-v/--version is required when --skip-update is used (bake tags will be empty otherwise)."
    exit 1
  fi

  if [[ -n "${pkg_url}" ]]; then
    if [[ "${pkg_url}" == http://* || "${pkg_url}" == https://* ]]; then
      deb_base_url="${pkg_url}"
    else
      packages_dir="${pkg_url}"
    fi
  fi

  # ---- Step 1: Update Dockerfiles ----
  if [[ "${skip_update}" == false && "${mode}" != "manifest" ]]; then
    log_info "=== Updating Dockerfiles ==="
    local update_args=("--version" "${VERSION}")
    if [[ -n "${packages_dir}" ]];     then update_args+=("--packages-dir" "${packages_dir}"); fi
    if [[ -n "${deb_base_url}" ]];     then update_args+=("--deb-base-url" "${deb_base_url}"); fi
    if [[ "${compute_sha}" == true ]]; then update_args+=("--compute-sha"); fi
    if [[ "${dry_run}"     == true ]]; then update_args+=("--dry-run"); fi
    ./update-version.sh "${update_args[@]}"
    echo ""
  fi

  if [[ "${generate_only}" == true ]]; then
    log_success "Dockerfiles updated. (-g only: skipping bake and build)"
    exit 0
  fi

  if [[ "${dry_run}" == true ]]; then
    log_info "Dry run: skipping bake generation and Docker build."
    exit 0
  fi

  # ---- Manifest mode (no build) ----
  if [[ "${mode}" == "manifest" ]]; then
    run_manifest_mode
    echo ""
    log_success "Done!"
    exit 0
  fi

  # ---- Step 2: Resolve local packages ----
  if [[ -n "${packages_dir}" ]]; then
    _setup_local_pkgs "${packages_dir}"
    trap '_cleanup_local_pkgs' EXIT
    echo ""
  fi

  # ---- Step 3: Generate bake file ----
  echo ""
  log_info "=== Generating Bake File ==="
  generate_bake

  # ---- Step 4: Build ----
  echo ""
  log_info "=== Building Images ==="
  local bake_args=("-f" "${BAKE_FILE}")
  if [[ "${no_cache}" == true ]]; then bake_args+=("--no-cache"); fi

  case "${mode}" in
  test)
    log_info "Building and loading locally..."
    docker buildx bake "${bake_args[@]}" test --progress plain --load
    ;;
  push)
    log_info "Building and pushing to: ${REGISTRY_PREFIXES[*]}..."
    docker buildx bake "${bake_args[@]}" push --progress plain --push
    ;;
  esac

  echo ""
  log_success "Done!"
}

main "$@"
