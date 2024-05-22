import random
import subprocess
import string
import sys
import tempfile
import time

from hypothesis import Phase

# to suppress the DeprecationWarning by crypt.crypt()
import warnings

with warnings.catch_warnings():
    warnings.filterwarnings("ignore", category=DeprecationWarning)
    from crypt import crypt
import os

CPU_COUNT = os.cpu_count()
VALGRIND_FLAGS = "--leak-check=full --error-exitcode=1 --trace-children=yes --quiet"
DEFAULT_PORT = 9000


phases = (Phase.explicit, Phase.reuse, Phase.generate, Phase.target)


def get_output(cmd):
    return subprocess.check_output(cmd, shell=True).decode()


def shuffle_password(passwd):
    passwd_as_list = list(passwd)
    random.shuffle(passwd_as_list)
    return "".join(passwd_as_list)


def gen_str(size, chars=string.ascii_uppercase + string.digits):
    return "".join(random.choice(chars) for _ in range(size))


def brute_cmd(passwd, alph, run_mode, brute_mode, threads=CPU_COUNT):
    hash = crypt(passwd, passwd)
    return f"./brute -H {hash} -l {len(str(passwd))} -a {alph} -{run_mode} -{brute_mode} -t {threads}"


def run_brute(passwd, alph, run_mode, brute_mode):
    return get_output(brute_cmd(passwd, alph, run_mode, brute_mode))


def run_client_server_process(cmd, log_file):
    return subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=log_file, shell=True)


def run_valgrind(passwd, alph, run_mode, brute_mode):
    cmd = brute_cmd(passwd, alph, run_mode, brute_mode)

    return get_output(f"valgrind {VALGRIND_FLAGS} {cmd}")


def run_valgrind_client_server(passwd, alph, brute_mode, client_flag, server_flag):
    client_cmd = brute_cmd(passwd, alph, client_flag, brute_mode)
    server_cmd = brute_cmd(passwd, alph, server_flag, brute_mode)

    client_valgrind_cmd = f"valgrind {VALGRIND_FLAGS} {client_cmd}"
    server_valgrind_cmd = f"valgrind {VALGRIND_FLAGS} {server_cmd}"

    server_valgrind_check = run_client_server_process(server_valgrind_cmd, sys.stderr)
    time.sleep(0.05)
    client_valgrind_check = run_client_server_process(client_valgrind_cmd, sys.stderr)
    try:
        client_valgrind_check.wait(timeout=5)
    except subprocess.TimeoutExpired:
        client_valgrind_check.kill()
        return False

    try:
        _, _ = server_valgrind_check.communicate(timeout=5)
    except subprocess.TimeoutExpired:
        server_valgrind_check.kill()
        return False

    return server_valgrind_check and client_valgrind_check


def run_valgrind_netcat_server(passwd, alph, brute_mode, client_flag, server_flag):
    client_cmd = brute_cmd(passwd, alph, client_flag, brute_mode)
    server_cmd = brute_cmd(passwd, alph, server_flag, brute_mode)

    server_valgrind_cmd = f"valgrind {VALGRIND_FLAGS} {server_cmd}"

    server_valgrind_check = run_client_server_process(server_valgrind_cmd, sys.stderr)
    time.sleep(0.05)
    netcat_process = run_client_server_process(
        f"nc localhost {DEFAULT_PORT}", sys.stderr
    )
    time.sleep(0.5)
    netcat_process.kill()

    client_proc = run_client_server_process(client_cmd, sys.stderr)
    try:
        client_proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        client_proc.kill()
        return False

    try:
        _, _ = server_valgrind_check.communicate(timeout=5)
    except subprocess.TimeoutExpired:
        server_valgrind_check.kill()
        return False

    return server_valgrind_check


def run_client_server(
    passwd, alph, brute_mode, client_flag, server_flag, client_log, server_log
):
    client_cmd = brute_cmd(passwd, alph, client_flag, brute_mode)
    server_cmd = brute_cmd(passwd, alph, server_flag, brute_mode)

    server_proc = run_client_server_process(server_cmd, server_log)
    time.sleep(0.05)
    client_proc = run_client_server_process(client_cmd, client_log)
    try:
        client_proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        client_proc.kill()
        return "Client timeout"

    try:
        output, _ = server_proc.communicate(timeout=5)
    except subprocess.TimeoutExpired:
        server_proc.kill()
        return "Server timeout"

    return output.decode()


def run_two_clients_server(
    passwd,
    alph,
    brute_mode,
    client_flag,
    server_flag,
    first_client_log,
    second_client_log,
    server_log,
):
    client_cmd = brute_cmd(passwd, alph, client_flag, brute_mode)
    server_cmd = brute_cmd(passwd, alph, server_flag, brute_mode)

    server_proc = run_client_server_process(server_cmd, server_log)
    time.sleep(0.05)
    first_client_proc = run_client_server_process(client_cmd, first_client_log)
    time.sleep(0.05)
    second_client_proc = run_client_server_process(client_cmd, second_client_log)
    try:
        first_client_proc.wait(timeout=5)
        second_client_proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        first_client_proc.kill()
        second_client_proc.kill()
        try:
            output, _ = server_proc.communicate(timeout=5)
        except subprocess.TimeoutExpired:
            server_proc.kill()
            return "Server timeout + client timeout"
        return "Client timeout"

    try:
        output, _ = server_proc.communicate(timeout=5)
    except subprocess.TimeoutExpired:
        server_proc.kill()
        return "Server timeout"

    return output.decode()


def capture_client_server_log(
    passwd, alph, brute_mode, client_flag, server_flag, func, num_of_clients=1
):
    first_client_log = tempfile.NamedTemporaryFile()
    server_log = tempfile.NamedTemporaryFile()
    if num_of_clients > 1:
        second_client_log = tempfile.NamedTemporaryFile()

    if num_of_clients == 1:
        result = func(
            passwd,
            alph,
            brute_mode,
            client_flag,
            server_flag,
            first_client_log,
            server_log,
        )
    else:
        result = func(
            passwd,
            alph,
            brute_mode,
            client_flag,
            server_flag,
            first_client_log,
            second_client_log,
            server_log,
        )

    first_client_log.flush()
    server_log.flush()
    if num_of_clients > 1:
        second_client_log.flush()

    if result != f"Password found: {passwd}\n":
        sys.stderr.write(f"Test failed. Output is {result}.\n")

        sys.stderr.write(f"Captured server log:\n")
        with open(server_log.name, "r") as output:
            sys.stderr.write(output.read())
        sys.stderr.write("End of captured server log.\n")
        sys.stderr.write(f"{'-' * 30}\n")

        sys.stderr.write("Captured client log:\n")
        with open(first_client_log.name, "r") as output:
            sys.stderr.write(output.read())
        sys.stderr.write("End of captured client log.\n")
        sys.stderr.write(f"{'-' * 30}\n")

        if num_of_clients > 1:
            sys.stderr.write("Captured second client log:\n")
            with open(second_client_log.name, "r") as output:
                sys.stderr.write(output.read())
            sys.stderr.write("End of captured second client log.\n")

    assert f"Password found: {passwd}\n" == result
