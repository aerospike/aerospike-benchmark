#!/usr/bin/env bash
# Same post-install checks as test_execute.bats, for hosts without bats.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
# shellcheck source=.github/bin/test/version_lib.sh
. "$SCRIPT_DIR/version_lib.sh"

asbench --help
asbench --version

expected="$(expected_version "$REPO_ROOT/VERSION")"
out="$(asbench --version 2>&1)"
assert_version_output "$out" "$expected"
echo "asbench reports $(expected_version_lines "$expected" | tr '\n' ' ')(from $expected)"

if command -v dpkg-query >/dev/null && dpkg-query -W aerospike-asbench >/dev/null 2>&1; then
	declared="$(dpkg-query -W -f='${Depends}' aerospike-asbench | tr ',|' '\n\n' | sed -E 's/\(.*\)//; s/^ +//; s/ +$//')"
	owner_of() { dpkg-query -S "$(readlink -f "$1")" | cut -d: -f1; }
elif command -v rpm >/dev/null && rpm -q aerospike-asbench >/dev/null 2>&1; then
	declared="$(rpm -q --requires aerospike-asbench | awk '{ print $1 }')"
	owner_of() { rpm -qf --qf '%{NAME}\n' "$(readlink -f "$1")"; }
else
	exit 0
fi

loaded="$(ldd "$(readlink -f "$(command -v asbench)")" | awk '$1 ~ /^lib(ssl|crypto)\.so/ { print $1, $3 }')"
for lib in libssl libcrypto; do
	if ! grep -q "^$lib\.so" <<< "$loaded"; then
		echo "error: asbench does not load $lib from the host" >&2
		exit 1
	fi
done
while read -r lib path; do
	owner="$(owner_of "$path" || true)"
	if [ -z "$owner" ] || [ "$owner" = aerospike-asbench ]; then
		echo "error: asbench loads $lib from $path, which no host package provides" >&2
		exit 1
	fi
	if ! grep -qxF "$owner" <<< "$declared"; then
		echo "error: asbench loads $lib from $owner, but the package declares: $(tr '\n' ' ' <<< "$declared")" >&2
		exit 1
	fi
	echo "asbench loads $lib from $owner, which the package declares"
done <<< "$loaded"
