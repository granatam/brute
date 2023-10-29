from datetime import timedelta
from hypothesis import given, strategies as st, settings
from utils import gen_str, run_valgrind

# TODO: Rewrite back test_valgrind
# TODO: Valgrind test for client-server interaction


def test_valgrind():
    alph = gen_str(5)
    passwd = gen_str(5, alph)

    assert run_valgrind(passwd, alph, "m", "y")
