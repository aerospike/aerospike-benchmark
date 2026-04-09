import lib
import subprocess
import signal


def test_output_file_unopenable():
    """
    When --output-file is given a path that can't be opened, asbench should exit
    with an error rather than crashing with a segfault.
    """
    lib.start()
    directory = lib.absolute_path("../../..")

    cmd = [
        "test_target/asbench",
        "-h",
        f"{lib.SERVER_IP}:{lib.PORT}",
        "-n",
        lib.NAMESPACE,
        "-s",
        lib.SET,
        "--output-file",
        ".",
    ]

    result = subprocess.run(cmd, cwd=directory)

    assert result.returncode != 0, "expected non-zero exit code"
    assert result.returncode != -signal.SIGSEGV, "crashed with SIGSEGV"
    assert result.returncode > 0, "killed by signal %d instead of exiting" % (-result.returncode)
