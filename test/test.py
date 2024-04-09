from datetime import timedelta
import string
import sys
import tempfile
from hypothesis import given, strategies as st, settings
from utils import (
    shuffle_password,
    gen_str,
    run_brute,
    run_client_server,
    run_two_clients_server,
)


# Password size is from 2 to 7, alphabet is just a shuffled password
@given(
    st.text(min_size=2, alphabet=string.ascii_letters, max_size=6),
    st.text(min_size=1, max_size=1, alphabet="smg"),
    st.text(min_size=1, max_size=1, alphabet="iry"),
)
@settings(deadline=timedelta(seconds=3))
def test_password_found(passwd, run_mode, brute_mode):
    alph = shuffle_password(passwd)

    assert (
        run_brute(passwd, alph, run_mode, brute_mode) == f"Password found: {passwd}\n"
    )


# Test for long alphabet and short password
@given(
    st.text(min_size=1, max_size=1, alphabet="smg"),
    st.text(min_size=1, max_size=1, alphabet="iry"),
)
@settings(deadline=timedelta(seconds=5))
def test_corner_cases(run_mode, brute_mode):
    long_alph = gen_str(5)
    short_password = gen_str(5, long_alph)

    assert (
        run_brute(short_password, long_alph, run_mode, brute_mode)
        == f"Password found: {short_password}\n"
    )


# Tests for client-server synchronous interaction with one client
@given(
    st.text(min_size=3, alphabet=string.ascii_letters, max_size=4),
    st.text(min_size=1, max_size=1, alphabet="iry"),
)
@settings(deadline=timedelta(seconds=5))
def test_client_server(passwd, brute_mode):
    alph = shuffle_password(passwd)
    
    with tempfile.NamedTemporaryFile() as f:
        result = run_client_server(passwd, alph, brute_mode, f)

        f.flush()
        if result != f"Password found: {passwd}\n":
            sys.stderr.write("Test failed. Captured stderr for this test:\n")
            with open(f.name, "rb") as output:
                for line in output:
                    sys.stderr.write(line.decode())
            sys.stderr.write("End of captured stderr.\n")
            
        assert f"Password found: {passwd}\n" == result 


# Tests for client-server synchronous interaction with two clients
@given(
    st.text(min_size=4, alphabet=string.ascii_letters, max_size=5),
    st.text(min_size=1, max_size=1, alphabet="iry"),
)
@settings(deadline=timedelta(seconds=5))
def test_two_clients_server(passwd, brute_mode):
    alph = shuffle_password(passwd)
    
    with tempfile.NamedTemporaryFile() as f:
        result = run_two_clients_server(passwd, alph, brute_mode, f)

        f.flush()
        if result != f"Password found: {passwd}\n":
            sys.stderr.write("Test failed. Captured stderr for this test:\n")
            with open(f.name, "rb") as output:
                for line in output:
                    sys.stderr.write(line.decode())
            sys.stderr.write("End of captured stderr.\n")
            
        assert f"Password found: {passwd}\n" == result 
