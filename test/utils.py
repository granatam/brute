from contextlib import redirect_stderr
import io
import random
import subprocess
import string
import sys
import time

# to suppress the DeprecationWarning by crypt.crypt()
import warnings

with warnings.catch_warnings():
    warnings.filterwarnings("ignore", category=DeprecationWarning)
    from crypt import crypt
import os

CPU_COUNT = os.cpu_count()


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


def run_two_clients_server(passwd, alph, brute_mode, client_flag, server_flag, file):
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
