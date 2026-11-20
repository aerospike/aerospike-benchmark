#!/usr/bin/env bats
#
# Post-install smoke tests, run against an INSTALLED asbench.
#
# The version test compares what the installed binary reports against the
# repo's VERSION file, both the Version and the Build line. A package labelled
# with one version but carrying a binary stamped with another (a build that
# loses VERSION and falls back to `git describe`, or a stale rc binary in a
# later rc's package) fails here, in this repo's own CI, instead of downstream
# in the aerospike-tools bundle.
#
# Set EXPECTED_VERSION to verify a package built from a revision other than
# the checkout the tests run from; it still has to agree with the VERSION
# file on MAJOR.MINOR.PATCH.

setup() {
  REPO_ROOT="$(cd "$BATS_TEST_DIRNAME/../../.." && pwd)"
  VERSION_FILE="$REPO_ROOT/VERSION"
  load "$BATS_TEST_DIRNAME/version_lib.sh"
}

installed_with() {
  if command -v dpkg-query >/dev/null && dpkg-query -W aerospike-asbench >/dev/null 2>&1; then
    echo deb
  elif command -v rpm >/dev/null && rpm -q aerospike-asbench >/dev/null 2>&1; then
    echo rpm
  fi
}

loaded_openssl() {
  ldd "$(readlink -f "$(command -v asbench)")" | awk '$1 ~ /^lib(ssl|crypto)\.so/ { print $1, $3 }'
}

owning_package() {
  local file
  file="$(readlink -f "$1")"
  case "$(installed_with)" in
    deb) dpkg-query -S "$file" | cut -d: -f1 ;;
    rpm) rpm -qf --qf '%{NAME}\n' "$file" ;;
  esac
}

declared_dependencies() {
  case "$(installed_with)" in
    deb) dpkg-query -W -f='${Depends}' aerospike-asbench | tr ',|' '\n\n' | sed -E 's/\(.*\)//; s/^ +//; s/ +$//' ;;
    rpm) rpm -q --requires aerospike-asbench | awk '{ print $1 }' ;;
  esac
}

@test "can run asbench" {
  run asbench --help
  [ "$status" -eq 0 ]
}

@test "asbench reports version" {
  run asbench --version
  [ "$status" -eq 0 ]
}

@test "asbench reports the version from the VERSION file" {
  local expected
  expected="$(expected_version "$VERSION_FILE")"

  run asbench --version
  [ "$status" -eq 0 ]
  echo "expected (from $expected):"
  expected_version_lines "$expected"
  echo "reported:"
  echo "$output"
  assert_version_output "$output" "$expected"
}

@test "asbench loads OpenSSL from a host package" {
  [ -n "$(installed_with)" ] || skip "asbench was not installed from a deb or rpm"

  run loaded_openssl
  echo "$output"
  [ "$status" -eq 0 ]
  grep -q '^libssl\.so' <<< "$output"
  grep -q '^libcrypto\.so' <<< "$output"

  local lib path owner
  while read -r lib path; do
    owner="$(owning_package "$path")"
    echo "$lib -> $path (${owner:-no package})"
    [ -n "$owner" ]
    [ "$owner" != aerospike-asbench ]
  done <<< "$output"
}

@test "the asbench package declares the OpenSSL it loads" {
  [ -n "$(installed_with)" ] || skip "asbench was not installed from a deb or rpm"

  local declared loaded
  declared="$(declared_dependencies)"
  loaded="$(loaded_openssl)"
  echo "declared: $(tr '\n' ' ' <<< "$declared")"
  [ -n "$loaded" ]

  local lib path owner
  while read -r lib path; do
    owner="$(owning_package "$path")"
    echo "$lib comes from ${owner:-no package}"
    [ -n "$owner" ]
    grep -qxF "$owner" <<< "$declared"
  done <<< "$loaded"
}
