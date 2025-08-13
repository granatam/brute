from datetime import timedelta

import pytest
from hypothesis import given, settings
from hypothesis.strategies import data
from testrunner import (
    CLIENT_MODES,
    SERVER_MODES,
    SIMPLE_MODES,
    BruteMode,
    CommandMode,
    Config,
    RunMode,
    _TestRunner,
    phases,
    port_st,
)


@pytest.mark.parametrize("run_mode", SIMPLE_MODES)
@given(data=data())
@settings(deadline=timedelta(seconds=3), phases=phases, max_examples=5)
def test_valgrind_simple(data, run_mode):
    _TestRunner(
        data,
        Config(
            (2, 3),
            (2, 3),
            run_mode=run_mode,
            brute_mode_pool=[BruteMode.ITERATIVE, BruteMode.RECURSIVE],
        ),
    ).run(CommandMode.VALGRIND)


@pytest.mark.parametrize("client_mode", CLIENT_MODES)
@pytest.mark.parametrize("server_mode", SERVER_MODES)
@given(data=data(), port=port_st)
@settings(deadline=timedelta(seconds=5), phases=phases, max_examples=5)
def test_valgrind_one_client_server(data, client_mode, server_mode, port):
    _TestRunner(
        data,
        Config(
            (2, 3),
            (2, 3),
            run_mode=server_mode,
            client_run_modes=[client_mode],
            port=port,
        ),
    ).run(CommandMode.VALGRIND)


@pytest.mark.parametrize("first_client_mode", CLIENT_MODES)
@pytest.mark.parametrize("second_client_mode", CLIENT_MODES)
@pytest.mark.parametrize("server_mode", SERVER_MODES)
@given(data=data(), port=port_st)
@settings(deadline=timedelta(seconds=5), phases=phases, max_examples=5)
def test_valgrind_two_clients_server(
    data, first_client_mode, second_client_mode, server_mode, port
):
    _TestRunner(
        data,
        Config(
            (4, 5),
            (4, 5),
            run_mode=server_mode,
            brute_mode_pool=[BruteMode.ITERATIVE, BruteMode.RECURSIVE],
            client_run_modes=[first_client_mode, second_client_mode],
            port=port,
        ),
    ).run(CommandMode.VALGRIND)


@pytest.mark.parametrize("client_mode", CLIENT_MODES)
@pytest.mark.parametrize("server_mode", SERVER_MODES)
@given(data=data(), port=port_st)
@settings(deadline=timedelta(seconds=5), phases=phases, max_examples=5)
def test_valgrind_netcat_client_server(data, client_mode, server_mode, port):
    _TestRunner(
        data,
        Config(
            (2, 3),
            (2, 3),
            run_mode=server_mode,
            brute_mode_pool=[BruteMode.ITERATIVE, BruteMode.RECURSIVE],
            client_run_modes=[RunMode.NETCAT, client_mode],
            port=port,
        ),
    ).run(CommandMode.VALGRIND)
