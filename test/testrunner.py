import os
import socket
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

from hypothesis import Phase, settings

MAX_EXAMPLES = os.getenv("HYPOTHESIS_MAX_EXAMPLES", "50")
settings.register_profile("custom", max_examples=int(MAX_EXAMPLES))
settings.load_profile("custom")

phases = (Phase.explicit, Phase.reuse, Phase.generate, Phase.target)

CPU_COUNT = os.cpu_count() or 8
VALGRIND_FLAGS = (
    "--leak-check=full --error-exitcode=1 --trace-children=yes --quiet"
)


class CommandMode(str, Enum):
    BASIC = "./brute"
    VALGRIND = f"valgrind {VALGRIND_FLAGS} ./brute"
    PERF = "time ./brute"


class RunMode(str, Enum):
    SINGLE = "single"
    MULTI = "multi"
    GENERATOR = "gen"
    SYNC_CLIENT = "client"
    SYNC_SERVER = "server"
    ASYNC_CLIENT = "async-client"
    ASYNC_SERVER = "async-server"
    REACTOR_SERVER = "reactor-server"
    NETCAT = "nc"  # Special case, used for client's behavior imitation


class BruteMode(str, Enum):
    ITERATIVE = "i"
    RECURSIVE = "r"
    RECURSIVE_GEN = "y"


SIMPLE_MODES = [RunMode.SINGLE, RunMode.MULTI, RunMode.GENERATOR]
SERVER_MODES = [
    RunMode.SYNC_SERVER,
    RunMode.ASYNC_SERVER,
    RunMode.REACTOR_SERVER,
]
CLIENT_MODES = [RunMode.SYNC_CLIENT, RunMode.ASYNC_CLIENT]


PORT_RANGE_START = 9000
PORT_RANGE_END = 9999


# Partition port range per pytest-xdist worker so parallel workers never collide.
def _worker_port_range():
    worker = os.environ.get("PYTEST_XDIST_WORKER", "")
    count_str = os.environ.get("PYTEST_XDIST_WORKER_COUNT", "1")
    if not (worker.startswith("gw") and worker[2:].isdigit()):
        return PORT_RANGE_START, PORT_RANGE_END
    try:
        idx = int(worker[2:])
        count = max(1, int(count_str))
        size = (PORT_RANGE_END - PORT_RANGE_START + 1) // count
        start = PORT_RANGE_START + idx * size
        end = start + size - 1 if idx < count - 1 else PORT_RANGE_END
        return start, end
    except (ValueError, TypeError):
        return PORT_RANGE_START, PORT_RANGE_END


_start, _end = _worker_port_range()
_next_port = _start


def wait_for_valgrind(port: int, timeout: float) -> bool:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
                s.settimeout(1.0)
                s.connect(("127.0.0.1", port))
            return True
        except OSError:
            time.sleep(0.1)
    return False


def get_free_port() -> int:
    global _next_port
    start, end = _worker_port_range()
    port = _next_port
    for _ in range(end - start + 1):
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            try:
                sock.bind(("", port))
                _next_port = start if port >= end else port + 1
                return port
            except OSError:
                pass
        port = start if port >= end else port + 1
    raise RuntimeError("No free ports")


def run_brute(
    mode: CommandMode,
    passwd: str,
    alph: str,
    run_mode: RunMode,
    brute_mode: BruteMode,
    log_file,  # IO[AnyStr]
    port: int,
    cpu_count: int = CPU_COUNT,
):
    if run_mode == RunMode.NETCAT:
        cmd = f"nc localhost {port}"
    else:
        hash = crypt(passwd, passwd)
        cmd = f"{mode.value} -H {hash} -l {len(str(passwd))} -a {alph} --{run_mode.value} -{brute_mode.value} -T {cpu_count} -p {port}"
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


class _TestRunner:
    def __init__(self, data, config: Config = Config()):
        self.config = config
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
            out = (
                proc.communicate(timeout=timeout)[0].decode()
                if capture_output
                else None
            )
            return out, exit_code
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
        with open(stderr_log.name) as f:
            stderr_text = f.read()

        fail = output != expected or (
            cmd_mode == CommandMode.VALGRIND and valgrind_fail
        )
        if not fail:
            return

        error_msg = [
            f"Test failed. Output: {output.strip()!r}. Expected: {expected.strip()!r}.",
            f"Command to reproduce: {cmd!r}.",
            f"Stderr log:\n{stderr_text.strip()}",
            f"{'-' * 30}\n",
        ]
        for i, (_, client_cmd, _, client_log) in enumerate(client_data or ()):
            with open(client_log.name) as f:
                error_msg += [
                    f"Client #{i} command: {client_cmd!r}",
                    f"Client #{i} log:\n{f.read().strip()}",
                    f"{'-' * 30}\n",
                ]
        sys.stderr.write("\n".join(error_msg))
        assert False, "Output does not match expected. See stderr for details."

    def run(self, cmd_mode=CommandMode.BASIC):
        brute_mode, alph, password = self.generated_params
        port = get_free_port()
        stderr_log = tempfile.NamedTemporaryFile()
        cmd, main_proc = run_brute(
            cmd_mode,
            password,
            alph,
            self.config.run_mode,
            brute_mode,
            stderr_log,
            port,
        )

        client_data = []
        if self.config.client_run_modes:
            # Under valgrind server startup is slow.
            timeout = 10.0 if cmd_mode == CommandMode.VALGRIND else 5.0
            if not wait_for_valgrind(port, timeout=timeout):
                main_proc.kill()
                main_proc.wait(timeout=2)
                with open(stderr_log.name) as f:
                    stderr_text = f.read()
                assert False, (
                    f"Server did not start listening on port {port} within {timeout}s. "
                    f"Stderr:\n{stderr_text}"
                )
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
                port,
            )
            client_data.append(
                (client_mode, client_cmd, client_proc, client_stderr_log)
            )

        output, main_ec = self.wait_for_process(
            self.config.run_mode, main_proc, 5
        )
        valgrind_fail = cmd_mode == CommandMode.VALGRIND and main_ec == 1
        for run_mode, _, client_proc, _ in client_data:
            _, ec = self.wait_for_process(run_mode, client_proc, 5, False)
            valgrind_fail |= cmd_mode == CommandMode.VALGRIND and ec == 1

        self.validate_output(
            cmd_mode,
            output,
            f"Password found: {password}\n",
            cmd,
            stderr_log,
            client_data,
            valgrind_fail,
        )
