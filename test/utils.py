import random
import subprocess
import string
# to suppress the DeprecationWarning by crypt.crypt()
import warnings
with warnings.catch_warnings():
    warnings.filterwarnings("ignore", category=DeprecationWarning)
    from crypt import crypt


def get_output(cmd):
    return subprocess.check_output(cmd, shell=True).decode()


def shuffle_password(passwd):
    passwd_as_list = list(passwd)
    random.shuffle(passwd_as_list)
    return ''.join(passwd_as_list)


def gen_alph(size, chars=string.ascii_uppercase + string.digits):
    return ''.join(random.choice(chars) for _ in range(size))


def gen_password_from_alph(size, alph):
    return ''.join(random.choice(alph) for _ in range(size))


def run_brute(passwd, alph, run_mode, brute_mode):
    hash = crypt(passwd, passwd)
    brute = './main -h {} -l {} -a {} -{} -{}'

    return get_output(brute.format(hash, len(str(passwd)), alph, run_mode, brute_mode))