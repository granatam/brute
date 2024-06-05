from datetime import timedelta
from hypothesis import given, strategies as st, settings
from utils import gen_str, run_valgrind, run_valgrind_client_server, run_valgrind_netcat_server, phases


@given(
    st.text(min_size=1, max_size=1, alphabet="smg"),
    st.text(min_size=1, max_size=1, alphabet="ir"),
)
@settings(deadline=timedelta(seconds=20), phases=phases)
def test_valgrind(run_mode, brute_mode):
    alph = gen_str(5)
    passwd = gen_str(5, alph)

    assert run_valgrind(passwd, alph, run_mode, brute_mode)


@given(
    st.text(min_size=1, max_size=1, alphabet="Sw"),
    st.text(min_size=1, max_size=1, alphabet="cv"),
    st.text(min_size=1, max_size=1, alphabet="ir"),
)
@settings(deadline=timedelta(seconds=20), phases=phases)
def test_valgrind_client_server(server_flag, client_flag, brute_mode):
    alph = gen_str(5)
    passwd = gen_str(5, alph)

    assert run_valgrind_client_server(
        passwd, alph, brute_mode, client_flag, server_flag, port=9008
    )


@given(
    st.text(min_size=1, max_size=1, alphabet="w"),
    st.text(min_size=1, max_size=1, alphabet="cv"),
    st.text(min_size=1, max_size=1, alphabet="ir"),
)
@settings(deadline=timedelta(seconds=20), phases=phases)
def test_valgrind_netcat_server(server_flag, client_flag, brute_mode):
    alph = gen_str(5)
    passwd = gen_str(5, alph)

    assert run_valgrind_netcat_server(
        passwd, alph, brute_mode, client_flag, server_flag, port=9009
    )
