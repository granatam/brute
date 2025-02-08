from datetime import timedelta

from hypothesis import given, settings
from hypothesis.strategies import data
from testrunner import Config, RunMode, _TestRunner, phases


# Synchronous server, one synchronous client
@given(data=data())
@settings(deadline=timedelta(seconds=5), phases=phases)
def test_sync_client_sync_server(data):
    _TestRunner(
        Config(
            (2, 3),
            (2, 3),
            run_mode=RunMode.SYNC_SERVER,
            client_run_modes=[RunMode.SYNC_CLIENT],
            port=9001,
        )
    ).run(data)


# Synchronous server, two synchronous clients
@given(data=data())
@settings(deadline=timedelta(seconds=5), phases=phases)
def test_two_sync_clients_sync_server(data):
    _TestRunner(
        Config(
            (4, 5),
            (4, 5),
            run_mode=RunMode.SYNC_SERVER,
            client_run_modes=[RunMode.SYNC_CLIENT, RunMode.SYNC_CLIENT],
            port=9002,
        )
    ).run(data)


# Asynchronous server, one synchronous client
@given(data=data())
@settings(deadline=timedelta(seconds=5), phases=phases)
def test_sync_client_async_server(data):
    _TestRunner(
        Config(
            (2, 3),
            (2, 3),
            run_mode=RunMode.ASYNC_SERVER,
            client_run_modes=[RunMode.SYNC_CLIENT],
            port=9003,
        )
    ).run(data)


# Asynchronous server, two synchronous clients
@given(data=data())
@settings(deadline=timedelta(seconds=5), phases=phases)
def test_two_sync_clients_async_server(data):
    _TestRunner(
        Config(
            (4, 5),
            (4, 5),
            run_mode=RunMode.ASYNC_SERVER,
            client_run_modes=[RunMode.SYNC_CLIENT, RunMode.SYNC_CLIENT],
            port=9004,
        )
    ).run(data)


# Asynchronous server, one asynchronous client
@given(data=data())
@settings(deadline=timedelta(seconds=5), phases=phases)
def test_async_client_async_server(data):
    _TestRunner(
        Config(
            (2, 3),
            (2, 3),
            run_mode=RunMode.ASYNC_SERVER,
            client_run_modes=[RunMode.SYNC_CLIENT],
            port=9005,
        )
    ).run(data)


# Asynchronous server, two asynchronous clients
@given(data=data())
@settings(deadline=timedelta(seconds=5), phases=phases)
def test_two_async_clients_async_server(data):
    _TestRunner(
        Config(
            (4, 5),
            (4, 5),
            run_mode=RunMode.ASYNC_SERVER,
            client_run_modes=[RunMode.ASYNC_CLIENT, RunMode.ASYNC_CLIENT],
            port=9006,
        )
    ).run(data)


# Synchronous server, one asynchronous client
@given(data=data())
@settings(deadline=timedelta(seconds=5), phases=phases)
def test_async_client_sync_server(data):
    _TestRunner(
        Config(
            (2, 3),
            (2, 3),
            run_mode=RunMode.SYNC_SERVER,
            client_run_modes=[RunMode.ASYNC_CLIENT],
            port=9007,
        )
    ).run(data)


# Synchronous server, two asynchronous clients
@given(data=data())
@settings(deadline=timedelta(seconds=5), phases=phases)
def test_two_async_clients_sync_server(data):
    _TestRunner(
        Config(
            (4, 5),
            (4, 5),
            run_mode=RunMode.SYNC_SERVER,
            client_run_modes=[RunMode.ASYNC_CLIENT, RunMode.ASYNC_CLIENT],
            port=9008,
        )
    ).run(data)


# TODO: Ultimate test - 4 clients, 1 server and long password?
