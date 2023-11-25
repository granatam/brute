from datetime import timedelta
from hypothesis import given, strategies as st, settings
from utils import gen_str, run_valgrind

# TODO: Valgrind test for client-server interaction

@given(
    st.text(min_size=1, max_size=1, alphabet="smg"),
    st.text(min_size=1, max_size=1, alphabet="ir"),
)
@settings(deadline=None)
def test_valgrind(run_mode, brute_mode):
    alph = gen_str(5)
    passwd = gen_str(5, alph)

    assert run_valgrind(passwd, alph, run_mode, brute_mode)
