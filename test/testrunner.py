import os
import string
import subprocess
import sys
import tempfile
import time
import warnings
from dataclasses import dataclass, field
from enum import Enum

from hypothesis import strategies as st

with warnings.catch_warnings():
    warnings.filterwarnings("ignore", category=DeprecationWarning)
    try:
        from crypt import crypt
    except ImportError:  # No crypt library in Python 3.13
        from legacycrypt import crypt


from hypothesis import Phase

from hypothesis import settings

MAX_EXAMPLES = os.getenv("HYPOTHESIS_MAX_EXAMPLES", "100")
settings.register_profile("custom", max_examples=int(MAX_EXAMPLES))
settings.load_profile("custom")

phases = (Phase.explicit, Phase.reuse, Phase.generate, Phase.target)

CPU_COUNT = os.cpu_count() or 8
VALGRIND_FLAGS = (
    "--leak-check=full --error-exitcode=1 --trace-children=yes --quiet"
)
DEFAULT_PORT = 8081


class CommandMode(str, Enum):
    BASIC = "./brute"
    VALGRIND = f"valgrind {VALGRIND_FLAGS} ./brute"
    PERF = "time ./brute"


class RunMode(str, Enum):
    SINGLE = "s"
    MULTI = "m"
    GENERATOR = "g"
    SYNC_CLIENT = "c"
    SYNC_SERVER = "S"
    ASYNC_CLIENT = "v"
    ASYNC_SERVER = "w"
    REACTOR_SERVER = "R"
    NETCAT = "nc"  # Special case, used for client's behavior imitation


class BruteMode(str, Enum):
    ITERATIVE = "i"
    RECURSIVE = "r"
    RECURSIVE_GEN = "y"


def run_brute(
    mode: CommandMode,
    passwd: str,
    alph: str,
    run_mode: RunMode,
    brute_mode: BruteMode,
    log_file,  # IO[AnyStr]
    cpu_count: int = CPU_COUNT,
    port: int = DEFAULT_PORT,
):
    if run_mode == RunMode.NETCAT:
        cmd = f"nc localhost {port}"
    else:
        hash = crypt(passwd, passwd)
        cmd = f"{mode.value} -H {hash} -l {len(str(passwd))} -a {alph} -{run_mode.value} -{brute_mode.value} -T {cpu_count} -p {port}"
    return cmd, subprocess.Popen(
        cmd, stdout=subprocess.PIPE, stderr=log_file, shell=True
    )


@dataclass
class Config:
    password_bounds: tuple[int, int] = (3, 4)
    alphabet_bounds: tuple[int, int] = (3, 4)
    brute_mode_pool: list[BruteMode] = field(
        default_factory=lambda: [
            BruteMode.ITERATIVE,
            BruteMode.RECURSIVE,
            BruteMode.RECURSIVE_GEN,
        ]
    )
    run_mode: RunMode = RunMode.SINGLE
    client_run_modes: list[RunMode] = field(default_factory=list)
    cpu_count: int = CPU_COUNT
    port: int = DEFAULT_PORT


class _TestRunner:
    def __init__(self, data, config: Config = Config()):
        self.config = config
        if os.getenv("ASAN"):
            self.config.brute_mode_pool = [
                BruteMode.ITERATIVE,
                BruteMode.RECURSIVE,
            ]
        self.generated_params = self.generate_test_params(data)

    def generate_test_params(self, data):
        brute_mode = data.draw(st.sampled_from(self.config.brute_mode_pool))
        alph = data.draw(
            st.text(
                min_size=self.config.alphabet_bounds[0],
                max_size=self.config.alphabet_bounds[1],
                alphabet=string.ascii_letters,
            )
        )
        password = data.draw(
            st.text(
                min_size=self.config.password_bounds[0],
                max_size=self.config.password_bounds[1],
                alphabet=alph,
            )
        )
        return brute_mode, alph, password

    def wait_for_process(self, run_mode, proc, timeout, capture_output=True):
        if run_mode == RunMode.NETCAT:
            proc.kill()
            return None, 0

        try:
            exit_code = proc.wait(timeout=timeout)
            if capture_output:
                output, _ = proc.communicate(timeout=timeout)
                return output.decode(), exit_code
            else:
                return None, exit_code
        except subprocess.TimeoutExpired:
            proc.kill()
            return "timeout" if capture_output else None, 1

    def validate_output(
        self,
        cmd_mode,
        output,
        expected,
        cmd,
        stderr_log,
        client_data=None,
        valgrind_fail=False,
    ):
        with open(stderr_log.name, "r") as log:
            stderr_log = log.read()

        if (
            output != expected
            or cmd_mode == CommandMode.VALGRIND
            and valgrind_fail
        ):
            error_msg = [
                f"Test failed. Output: {output.strip()!r}. Expected: {expected.strip()!r}.",
                f"Command to reproduce: {cmd!r}.",
                f"Stderr log:\n{stderr_log.strip()}",
                f"{'-' * 30}\n",
            ]
            if client_data:
                for i, (_, client_cmd, _, client_log) in enumerate(client_data):
                    with open(client_log.name, "r") as log:
                        client_log = log.read()
                    error_msg.extend(
                        [
                            f"Client #{i} command: {client_cmd!r}",
                            f"Client #{i} log:\n{client_log.strip()}",
                            f"{'-' * 30}\n",
                        ]
                    )
            sys.stderr.write("\n".join(error_msg))
            assert False, (
                "Output does not match expected. See stderr for details."
            )

    def run(self, cmd_mode=CommandMode.BASIC):
        brute_mode, alph, password = self.generated_params
        expected = f"Password found: {password}\n"

        stderr_log = tempfile.NamedTemporaryFile()
        cmd, main_proc = run_brute(
            cmd_mode,
            password,
            alph,
            self.config.run_mode,
            brute_mode,
            stderr_log,
            port=self.config.port,
        )

        client_data = []
        for client_mode in self.config.client_run_modes:
            time.sleep(0.05)
            client_stderr_log = tempfile.NamedTemporaryFile()
            client_cmd, client_proc = run_brute(
                cmd_mode,
                password,
                alph,
                client_mode,
                brute_mode,
                client_stderr_log,
                port=self.config.port,
            )
            client_data.append(
                (client_mode, client_cmd, client_proc, client_stderr_log)
            )

        valgrind_fail = False
        output, exit_code = self.wait_for_process(
            self.config.run_mode, main_proc, 5
        )
        if cmd_mode == CommandMode.VALGRIND:
            valgrind_fail |= exit_code == 1
        for run_mode, _, client_proc, _ in client_data:
            _, exit_code = self.wait_for_process(
                run_mode, client_proc, 5, False
            )
            if cmd_mode == CommandMode.VALGRIND:
                valgrind_fail |= exit_code == 1

        self.validate_output(
            cmd_mode,
            output,
            expected,
            cmd,
            stderr_log,
            client_data,
            valgrind_fail,
        )
