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


def run_valgrind(passwd, alph, run_mode, brute_mode):
    cmd = brute_cmd(passwd, alph, run_mode, brute_mode)

    return get_output(
        f"valgrind --leak-check=full --error-exitcode=1 --trace-children=yes --quiet {cmd}"
    )


def run_client_server(passwd, alph, brute_mode, client_flag, server_flag, file):
    client_cmd = brute_cmd(passwd, alph, client_flag, brute_mode)
    server_cmd = brute_cmd(passwd, alph, server_flag, brute_mode)

    server_proc = subprocess.Popen(
        server_cmd, stdout=subprocess.PIPE, stderr=file, shell=True
    )
    time.sleep(0.05)
    client_proc = subprocess.Popen(
        client_cmd, stdout=subprocess.PIPE, stderr=file, shell=True
    )
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
    passwd, alph, brute_mode, client_flag, server_flag, file
):
    client_cmd = brute_cmd(passwd, alph, client_flag, brute_mode)
    server_cmd = brute_cmd(passwd, alph, server_flag, brute_mode)

    server_proc = subprocess.Popen(
        server_cmd, stdout=subprocess.PIPE, stderr=file, shell=True
    )
    time.sleep(0.05)
    first_client_proc = subprocess.Popen(
        client_cmd, stdout=subprocess.PIPE, stderr=file, shell=True
    )
    time.sleep(0.05)
    second_client_proc = subprocess.Popen(
        client_cmd, stdout=subprocess.PIPE, stderr=file, shell=True
    )
    try:
        first_client_proc.wait(timeout=5)
        second_client_proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        first_client_proc.kill()
        second_client_proc.kill()
        return "Client timeout"

    try:
        output, _ = server_proc.communicate(timeout=5)
    except subprocess.TimeoutExpired:
        server_proc.kill()
        return "Server timeout"

    return output.decode()


def capture_client_server_log(
    passwd, alph, brute_mode, client_flag, server_flag, func
):
    with tempfile.NamedTemporaryFile() as f:
        result = func(passwd, alph, brute_mode, client_flag, server_flag, f)

        f.flush()
        if result != f"Password found: {passwd}\n":
            sys.stderr.write(f"Test failed. Output is {result}. Captured stderr for this test:\n")
            with open(f.name, "rb") as output:
                for line in output:
                    sys.stderr.write(line.decode())
            sys.stderr.write("End of captured stderr.\n")

        assert f"Password found: {passwd}\n" == result
