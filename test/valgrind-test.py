from datetime import timedelta

from hypothesis import given, settings
from hypothesis.strategies import data
from testrunner import (BruteMode, CommandMode, Config, RunMode, _TestRunner,
                        phases)


@given(data=data())
@settings(deadline=timedelta(seconds=5), phases=phases, max_examples=5)
def test_valgrind_single(data):
    _TestRunner(
        data,
        Config(
            (2, 3),
            (2, 3),
            run_mode=RunMode.SINGLE,
            brute_mode_pool=[BruteMode.ITERATIVE, BruteMode.RECURSIVE],
        ),
    ).run(CommandMode.VALGRIND)


@given(data=data())
@settings(deadline=timedelta(seconds=5), phases=phases, max_examples=5)
def test_valgrind_multi(data):
    _TestRunner(
        data,
        Config(
            (2, 3),
            (2, 3),
            run_mode=RunMode.MULTI,
            brute_mode_pool=[BruteMode.ITERATIVE, BruteMode.RECURSIVE],
        ),
    ).run(CommandMode.VALGRIND)


@given(data=data())
@settings(deadline=timedelta(seconds=5), phases=phases, max_examples=5)
def test_valgrind_gen(data):
    _TestRunner(
        data,
        Config(
            (2, 3),
            (2, 3),
            run_mode=RunMode.GENERATOR,
            brute_mode_pool=[BruteMode.ITERATIVE, BruteMode.RECURSIVE],
        ),
    ).run(CommandMode.VALGRIND)


# Synchronous server, one synchronous client
@given(data=data())
@settings(deadline=timedelta(seconds=5), phases=phases, max_examples=5)
def test_valgrind_sync_client_sync_server(data):
    _TestRunner(
        data,
        Config(
            (2, 3),
            (2, 3),
            run_mode=RunMode.SYNC_SERVER,
            brute_mode_pool=[BruteMode.ITERATIVE, BruteMode.RECURSIVE],
            client_run_modes=[RunMode.SYNC_CLIENT],
            port=9009,
        ),
    ).run(CommandMode.VALGRIND)


# Asynchronous server, one synchronous client
@given(data=data())
@settings(deadline=timedelta(seconds=5), phases=phases, max_examples=5)
def test_valgrind_sync_client_async_server(data):
    _TestRunner(
        data,
        Config(
            (2, 3),
            (2, 3),
            run_mode=RunMode.ASYNC_SERVER,
            brute_mode_pool=[BruteMode.ITERATIVE, BruteMode.RECURSIVE],
            client_run_modes=[RunMode.SYNC_CLIENT],
            port=9010,
        ),
    ).run(CommandMode.VALGRIND)


# Asynchronous server, one asynchronous client
@given(data=data())
@settings(deadline=timedelta(seconds=5), phases=phases, max_examples=5)
def test_valgrind_async_client_async_server(data):
    _TestRunner(
        data,
        Config(
            (2, 3),
            (2, 3),
            run_mode=RunMode.ASYNC_SERVER,
            brute_mode_pool=[BruteMode.ITERATIVE, BruteMode.RECURSIVE],
            client_run_modes=[RunMode.SYNC_CLIENT],
            port=9011,
        ),
    ).run(CommandMode.VALGRIND)


# Synchronous server, one asynchronous client
@given(data=data())
@settings(deadline=timedelta(seconds=5), phases=phases, max_examples=5)
def test_valgrind_async_client_sync_server(data):
    _TestRunner(
        data,
        Config(
            (2, 3),
            (2, 3),
            run_mode=RunMode.SYNC_SERVER,
            brute_mode_pool=[BruteMode.ITERATIVE, BruteMode.RECURSIVE],
            client_run_modes=[RunMode.ASYNC_CLIENT],
            port=9012,
        ),
    ).run(CommandMode.VALGRIND)


@given(data=data())
@settings(deadline=timedelta(seconds=5), phases=phases, max_examples=5)
def test_valgrind_netcat_sync_client_async_server(data):
    _TestRunner(
        data,
        Config(
            (2, 3),
            (2, 3),
            run_mode=RunMode.ASYNC_SERVER,
            brute_mode_pool=[BruteMode.ITERATIVE, BruteMode.RECURSIVE],
            client_run_modes=[RunMode.NETCAT, RunMode.SYNC_CLIENT],
            port=9013,
        ),
    ).run(CommandMode.VALGRIND)


@given(data=data())
@settings(deadline=timedelta(seconds=5), phases=phases, max_examples=5)
def test_valgrind_netcat_async_client_async_server(data):
    _TestRunner(
        data,
        Config(
            (2, 3),
            (2, 3),
            run_mode=RunMode.ASYNC_SERVER,
            brute_mode_pool=[BruteMode.ITERATIVE, BruteMode.RECURSIVE],
            client_run_modes=[RunMode.NETCAT, RunMode.ASYNC_CLIENT],
            port=9014,
        ),
    ).run(CommandMode.VALGRIND)
