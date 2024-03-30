from datetime import timedelta
import string
from hypothesis import given, strategies as st, settings
from utils import shuffle_password, gen_str, run_brute, run_client_server


# Password size is from 2 to 7, alphabet is just a shuffled password
@given(
    st.text(min_size=2, alphabet=string.ascii_letters, max_size=6),
    st.text(min_size=1, max_size=1, alphabet="smg"),
    st.text(min_size=1, max_size=1, alphabet="iry"),
)
@settings(deadline=timedelta(seconds=3))
def test_password_found(passwd, run_mode, brute_mode):
    alph = shuffle_password(passwd)

    assert run_brute(
        passwd, alph, run_mode, brute_mode
    ) == "Password found: {}\n".format(passwd)


# Test for long alphabet and short password
@given(
    st.text(min_size=1, max_size=1, alphabet="smg"),
    st.text(min_size=1, max_size=1, alphabet="iry"),
)
@settings(deadline=timedelta(seconds=5))
def test_corner_cases(run_mode, brute_mode):
    long_alph = gen_str(5)
    short_password = gen_str(5, long_alph)

    assert run_brute(
        short_password, long_alph, run_mode, brute_mode
    ) == "Password found: {}\n".format(short_password)


@given(
    st.text(min_size=2, alphabet=string.ascii_letters, max_size=4),
    st.text(min_size=1, max_size=1, alphabet="iry"),
)
@settings(deadline=timedelta(seconds=5), max_examples=3)
def test_client_server(passwd, brute_mode):
    alph = shuffle_password(passwd)

    assert "Password found: {}\n".format(passwd) == run_client_server(
        passwd, alph, brute_mode
    )
