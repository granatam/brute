import random
import subprocess
import string
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
    return f"./main -H {hash} -l {len(str(passwd))} -a {alph} -{run_mode} -{brute_mode} -t {threads}"


def run_brute(passwd, alph, run_mode, brute_mode):
    return get_output(brute_cmd(passwd, alph, run_mode, brute_mode))


def run_valgrind(passwd, alph, run_mode, brute_mode):
    cmd = brute_cmd(passwd, alph, run_mode, brute_mode)

    return get_output(
        f"valgrind --leak-check=full --error-exitcode=1 --trace-children=yes --quiet {cmd}"
    )


def run_client_server(passwd, alph, brute_mode):
    client_cmd = brute_cmd(passwd, alph, "c", brute_mode)
    server_cmd = brute_cmd(passwd, alph, "S", brute_mode)

    server_proc = subprocess.Popen(server_cmd, stdout=subprocess.PIPE, shell=True)
    subprocess.Popen(client_cmd, stdout=subprocess.PIPE, shell=True).wait(timeout=2)
    try:
        output, _ = server_proc.communicate(timeout=2)
    except subprocess.TimeoutExpired:
        server_proc.kill()
        _, output = server_proc.communicate(timeout=1)

    return output.decode()
