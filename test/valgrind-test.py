from datetime import timedelta
from hypothesis import given, strategies as st, settings
from utils import gen_str, run_valgrind


@given(st.text(min_size=1, max_size=1, alphabet='m'), st.text(min_size=1, max_size=1, alphabet='i'))
@settings(deadline=timedelta(minutes=2))
def test_valgrind(run_mode, brute_mode):
    alph = gen_str(5)
    passwd = gen_str(5, alph)

    assert run_valgrind(passwd, alph, run_mode, brute_mode)
