#!/usr/bin/env bash
# Version assertions shared by test_execute.bats and test_execute.sh, so the
# two cannot drift.

# Resolve the version the installed binary is expected to report:
# EXPECTED_VERSION when set, otherwise the VERSION file. When both are present
# their MAJOR.MINOR.PATCH cores must agree -- a mismatch means CI handed the
# tests the wrong workflow output, not that the binary is wrong.
expected_version() {
	local version_file="$1" file_version="" expected
	if [ -f "$version_file" ]; then
		file_version="$(tr -d '[:space:]' <"$version_file")"
	fi
	# Set-but-empty is a lost workflow output, not "unset": GitHub renders an
	# unresolvable needs.<job>.outputs.<name> as the empty string rather than
	# failing the step, and ${VAR:-...} would silently fall back to the VERSION
	# file -- asserting a different claim than the workflow intended and
	# reporting it as a pass.
	if [ -n "${EXPECTED_VERSION+set}" ] && [ -z "$EXPECTED_VERSION" ]; then
		echo "EXPECTED_VERSION is set but empty -- a workflow output was lost" >&2
		return 1
	fi
	expected="${EXPECTED_VERSION:-$file_version}"
	if [ -z "$expected" ]; then
		echo "no VERSION file at $version_file and no EXPECTED_VERSION set" >&2
		return 1
	fi
	if [ -n "${EXPECTED_VERSION:-}" ] && [ -n "$file_version" ] &&
		[ "${EXPECTED_VERSION%%-*}" != "${file_version%%-*}" ]; then
		echo "EXPECTED_VERSION '$EXPECTED_VERSION' and VERSION file '$file_version' disagree" >&2
		return 1
	fi
	printf '%s\n' "$expected"
}

# The lines `asbench --version` must print for that string. print_version()
# splits on "-": the core goes on the Version line and the last field, if any,
# on a Build line.
#
#   2.2.10-rc1        Version 2.2.10 / Build rc1
#   2.2.10-abc123def  Version 2.2.10 / Build abc123def
#   2.2.10            Version 2.2.10
expected_version_lines() {
	local expected="$1" core
	core="${expected%%-*}"
	if ! printf '%s' "$core" | grep -qE '^[0-9]+\.[0-9]+\.[0-9]+$'; then
		echo "cannot parse a MAJOR.MINOR.PATCH core out of '$expected'" >&2
		return 1
	fi
	printf 'Version %s\n' "$core"
	if [ "$expected" != "$core" ]; then
		# print_version() splits on "-" and strtok skips trailing delimiters, so
		# a version ending in "-" yields no Build line at all. Emitting the
		# literal "Build " here would blame the binary for a malformed
		# expectation.
		local build="${expected##*-}"
		if [ -z "$build" ]; then
			echo "malformed version '$expected' (empty trailing field)" >&2
			return 1
		fi
		printf 'Build %s\n' "$build"
	fi
}

assert_version_output() {
	local output="$1" expected="$2" lines line
	lines="$(expected_version_lines "$expected")" || return 1

	while IFS= read -r line; do
		if ! printf '%s\n' "$output" | grep -qxF "$line"; then
			echo "expected '$line' (from $expected) in --version output:" >&2
			printf '%s\n' "$output" >&2
			return 1
		fi
	done <<<"$lines"

	# A stale binary stamped from `git describe` reports a Build line where the
	# release version has none, which the checks above cannot see.
	if [ "$expected" = "${expected%%-*}" ] && printf '%s\n' "$output" | grep -q '^Build '; then
		echo "unexpected Build line for version '$expected':" >&2
		printf '%s\n' "$output" >&2
		return 1
	fi
}
