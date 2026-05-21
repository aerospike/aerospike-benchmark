# Post-install smoke tests

The CI workflow verifies the install inline: `asbench --help` and `asbench --version`.

`test_execute.sh` and `test_execute.bats` are for local verification after you install the
package yourself (for example from CI artifacts or a local `.deb` / `.rpm`).
