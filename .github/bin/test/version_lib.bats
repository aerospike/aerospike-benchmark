#!/usr/bin/env bats
#
# Tests for the assertions in version_lib.sh -- the gate that decides whether a
# release ships. These check that it FAILS when it should, which is the half
# nothing else covers: a degraded assertion (say grep -qxF weakened to grep -qF)
# would let a mis-stamped binary through with every check still green.
#
# Pure string logic, no installed asbench, so this runs on every pull request.

setup() {
  load "$BATS_TEST_DIRNAME/version_lib.sh"
  VF="$BATS_TEST_TMPDIR/VERSION"
  unset EXPECTED_VERSION
}

# ---- expected_version -------------------------------------------------------

@test "expected_version reads the VERSION file when EXPECTED_VERSION is unset" {
  echo "2.2.10-rc1" > "$VF"
  run expected_version "$VF"
  [ "$status" -eq 0 ]
  [ "$output" = "2.2.10-rc1" ]
}

@test "expected_version strips surrounding whitespace from the VERSION file" {
  printf '  2.2.10-rc1 \n' > "$VF"
  run expected_version "$VF"
  [ "$status" -eq 0 ]
  [ "$output" = "2.2.10-rc1" ]
}

@test "expected_version prefers EXPECTED_VERSION when the cores agree" {
  echo "2.2.10-rc1" > "$VF"
  EXPECTED_VERSION="2.2.10-abc123def" run expected_version "$VF"
  [ "$status" -eq 0 ]
  [ "$output" = "2.2.10-abc123def" ]
}

@test "expected_version rejects an EXPECTED_VERSION whose core disagrees" {
  echo "2.2.10-rc1" > "$VF"
  EXPECTED_VERSION="3.0.0" run expected_version "$VF"
  [ "$status" -eq 1 ]
  [[ "$output" == *"disagree"* ]]
}

@test "expected_version rejects a set-but-empty EXPECTED_VERSION" {
  # A lost workflow output renders as "" and must not silently fall back to the
  # VERSION file, which would assert a different claim and report a pass.
  echo "2.2.10-rc1" > "$VF"
  EXPECTED_VERSION="" run expected_version "$VF"
  [ "$status" -eq 1 ]
  [[ "$output" == *"set but empty"* ]]
}

@test "expected_version fails with no VERSION file and no EXPECTED_VERSION" {
  run expected_version "$BATS_TEST_TMPDIR/absent"
  [ "$status" -eq 1 ]
}

# ---- expected_version_lines -------------------------------------------------

@test "expected_version_lines: a bare version has no Build line" {
  run expected_version_lines "2.2.10"
  [ "$status" -eq 0 ]
  [ "$output" = "Version 2.2.10" ]
}

@test "expected_version_lines: a two-field version gets a Build line" {
  run expected_version_lines "2.2.10-rc1"
  [ "$status" -eq 0 ]
  [ "$output" = "Version 2.2.10
Build rc1" ]
}

@test "expected_version_lines: git describe output uses the last field" {
  run expected_version_lines "2.2.9-15-gabcdef123"
  [ "$status" -eq 0 ]
  [ "$output" = "Version 2.2.9
Build gabcdef123" ]
}

@test "expected_version_lines rejects a version with no MAJOR.MINOR.PATCH core" {
  run expected_version_lines "garbage"
  [ "$status" -eq 1 ]
  [[ "$output" == *"cannot parse"* ]]
}

@test "expected_version_lines rejects an empty trailing field" {
  # print_version() prints no Build line for "2.2.10-", so expecting the literal
  # "Build " would blame the binary for a malformed expectation.
  run expected_version_lines "2.2.10-"
  [ "$status" -eq 1 ]
  [[ "$output" == *"malformed version"* ]]
}

# ---- assert_version_output --------------------------------------------------

@test "assert_version_output passes on a matching bare version" {
  run assert_version_output "Aerospike Benchmark
Version 2.2.10" "2.2.10"
  [ "$status" -eq 0 ]
}

@test "assert_version_output passes on a matching rc" {
  run assert_version_output "Aerospike Benchmark
Version 2.2.10
Build rc1" "2.2.10-rc1"
  [ "$status" -eq 0 ]
}

@test "assert_version_output catches a stale rc binary in a later rc's package" {
  run assert_version_output "Aerospike Benchmark
Version 2.2.10
Build rc1" "2.2.10-rc3"
  [ "$status" -eq 1 ]
  [[ "$output" == *"Build rc3"* ]]
}

@test "assert_version_output catches a bare binary where an rc was expected" {
  run assert_version_output "Aerospike Benchmark
Version 2.2.10" "2.2.10-rc1"
  [ "$status" -eq 1 ]
}

@test "assert_version_output catches a git describe binary where GA was expected" {
  # The tag-last pipeline's failure mode: make fell back to `git describe` and
  # the binary reports the PREVIOUS tag, so it carries a Build line GA has not.
  run assert_version_output "Aerospike Benchmark
Version 2.2.10
Build gabcdef123" "2.2.10"
  [ "$status" -eq 1 ]
  [[ "$output" == *"unexpected Build line"* ]]
}

@test "assert_version_output catches a wrong Version line" {
  run assert_version_output "Aerospike Benchmark
Version 2.2.9
Build rc1" "2.2.10-rc1"
  [ "$status" -eq 1 ]
}

@test "assert_version_output requires a whole-line match, not a substring" {
  # "Version 2.2.100" must not satisfy an expectation of "Version 2.2.10".
  run assert_version_output "Aerospike Benchmark
Version 2.2.100" "2.2.10"
  [ "$status" -eq 1 ]
}
