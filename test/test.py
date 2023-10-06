from datetime import timedelta
import string
from hypothesis import given, strategies as st, settings
from utils import shuffle_password, gen_alph, gen_password_from_alph, run_brute


# Password size is from 2 to 7, alphabet is just a shuffled password
@given(st.text(min_size=2, alphabet=string.ascii_letters + string.digits,
               max_size=7))
@settings(deadline=timedelta(seconds=3))
def test_password_found(passwd):
    alph = shuffle_password(passwd)

    (single, multi) = run_brute(passwd, alph)

    assert single == 'Password found: {}\n'.format(passwd) and single == multi


# Test for long alphabet and short password
def test_corner_cases():
    long_alph = gen_alph(15)
    short_password = gen_password_from_alph(5, long_alph)

    (single, multi) = run_brute(short_password, long_alph)

    assert single == 'Password found: {}\n'.format(short_password) \
        and single == multi