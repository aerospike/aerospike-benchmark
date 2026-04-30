# Post-install smoke tests

The CI workflow verifies the install inline: `asbench --help`.

`test_execute.sh` and `test_execute.bats` are available for local verification after
manual package installation. `install_from_jfrog.sh` installs the published package
from Artifactory before running them.
