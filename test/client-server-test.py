from datetime import timedelta

from hypothesis import given, settings
from hypothesis.strategies import data
from testrunner import Config, RunMode, _TestRunner, phases


# # Synchronous server, one synchronous client
# @given(data=data())
# @settings(deadline=timedelta(seconds=5), phases=phases)
# def test_sync_client_sync_server(data):
#     _TestRunner(
#         data,
#         Config(
#             (2, 3),
#             (2, 3),
#             run_mode=RunMode.SYNC_SERVER,
#             client_run_modes=[RunMode.SYNC_CLIENT],
#             port=9001,
#         ),
#     ).run()
#
#
# # Synchronous server, two synchronous clients
# @given(data=data())
# @settings(deadline=timedelta(seconds=5), phases=phases)
# def test_two_sync_clients_sync_server(data):
#     _TestRunner(
#         data,
#         Config(
#             (4, 5),
#             (4, 5),
#             run_mode=RunMode.SYNC_SERVER,
#             client_run_modes=[RunMode.SYNC_CLIENT, RunMode.SYNC_CLIENT],
#             port=9002,
#         ),
#     ).run()
#
#
# # Asynchronous server, one synchronous client
# @given(data=data())
# @settings(deadline=timedelta(seconds=5), phases=phases)
# def test_sync_client_async_server(data):
#     _TestRunner(
#         data,
#         Config(
#             (2, 3),
#             (2, 3),
#             run_mode=RunMode.ASYNC_SERVER,
#             client_run_modes=[RunMode.SYNC_CLIENT],
#             port=9003,
#         ),
#     ).run()
#
#
# # Asynchronous server, two synchronous clients
# @given(data=data())
# @settings(deadline=timedelta(seconds=5), phases=phases)
# def test_two_sync_clients_async_server(data):
#     _TestRunner(
#         data,
#         Config(
#             (4, 5),
#             (4, 5),
#             run_mode=RunMode.ASYNC_SERVER,
#             client_run_modes=[RunMode.SYNC_CLIENT, RunMode.SYNC_CLIENT],
#             port=9004,
#         ),
#     ).run()
#
#
# # Asynchronous server, one asynchronous client
# @given(data=data())
# @settings(deadline=timedelta(seconds=5), phases=phases)
# def test_async_client_async_server(data):
#     _TestRunner(
#         data,
#         Config(
#             (2, 3),
#             (2, 3),
#             run_mode=RunMode.ASYNC_SERVER,
#             client_run_modes=[RunMode.ASYNC_CLIENT],
#             port=9005,
#         ),
#     ).run()
#
#
# # Asynchronous server, two asynchronous clients
# @given(data=data())
# @settings(deadline=timedelta(seconds=5), phases=phases)
# def test_two_async_clients_async_server(data):
#     _TestRunner(
#         data,
#         Config(
#             (4, 5),
#             (4, 5),
#             run_mode=RunMode.ASYNC_SERVER,
#             client_run_modes=[RunMode.ASYNC_CLIENT, RunMode.ASYNC_CLIENT],
#             port=9006,
#         ),
#     ).run()
#
#
# # Synchronous server, one asynchronous client
# @given(data=data())
# @settings(deadline=timedelta(seconds=5), phases=phases)
# def test_async_client_sync_server(data):
#     _TestRunner(
#         data,
#         Config(
#             (2, 3),
#             (2, 3),
#             run_mode=RunMode.SYNC_SERVER,
#             client_run_modes=[RunMode.ASYNC_CLIENT],
#             port=9007,
#         ),
#     ).run()
#
#
# # Synchronous server, two asynchronous clients
# @given(data=data())
# @settings(deadline=timedelta(seconds=5), phases=phases)
# def test_two_async_clients_sync_server(data):
#     _TestRunner(
#         data,
#         Config(
#             (4, 5),
#             (4, 5),
#             run_mode=RunMode.SYNC_SERVER,
#             client_run_modes=[RunMode.ASYNC_CLIENT, RunMode.ASYNC_CLIENT],
#             port=9008,
#         ),
#     ).run()


# Reactor server, one synchronous client
@given(data=data())
@settings(deadline=timedelta(seconds=5), phases=phases)
def test_sync_client_reactor_server(data):
    _TestRunner(
        data,
        Config(
            (2, 3),
            (2, 3),
            run_mode=RunMode.REACTOR_SERVER,
            client_run_modes=[RunMode.SYNC_CLIENT],
            port=9015,
        ),
    ).run()


# Reactor server, two synchronous clients
@given(data=data())
@settings(deadline=timedelta(seconds=5), phases=phases)
def test_two_sync_clients_reactor_server(data):
    _TestRunner(
        data,
        Config(
            (4, 5),
            (4, 5),
            run_mode=RunMode.REACTOR_SERVER,
            client_run_modes=[RunMode.SYNC_CLIENT, RunMode.SYNC_CLIENT],
            port=9016,
        ),
    ).run()


# Reactor server, one asynchronous client
@given(data=data())
@settings(deadline=timedelta(seconds=5), phases=phases)
def test_async_client_reactor_server(data):
    _TestRunner(
        data,
        Config(
            (2, 3),
            (2, 3),
            run_mode=RunMode.REACTOR_SERVER,
            client_run_modes=[RunMode.SYNC_CLIENT],
            port=9017,
        ),
    ).run()


# Reactor server, two asynchronous clients
@given(data=data())
@settings(deadline=timedelta(seconds=5), phases=phases)
def test_two_async_clients_reactor_server(data):
    _TestRunner(
        data,
        Config(
            (4, 5),
            (4, 5),
            run_mode=RunMode.REACTOR_SERVER,
            client_run_modes=[RunMode.ASYNC_CLIENT, RunMode.ASYNC_CLIENT],
            port=9018,
        ),
    ).run()


# Synchronous server, one reactor client
@given(data=data())
@settings(deadline=timedelta(seconds=5), phases=phases)
def test_reactor_client_sync_server(data):
    _TestRunner(
        data,
        Config(
            (2, 3),
            (2, 3),
            run_mode=RunMode.SYNC_SERVER,
            client_run_modes=[RunMode.REACTOR_CLIENT],
            port=9019,
        ),
    ).run()


# Synchronous server, two reactor clients
@given(data=data())
@settings(deadline=timedelta(seconds=5), phases=phases)
def test_two_reactor_clients_sync_server(data):
    _TestRunner(
        data,
        Config(
            (4, 5),
            (4, 5),
            run_mode=RunMode.SYNC_SERVER,
            client_run_modes=[RunMode.REACTOR_CLIENT, RunMode.REACTOR_CLIENT],
            port=9020,
        ),
    ).run()


# Asynchronous server, one reactor client
@given(data=data())
@settings(deadline=timedelta(seconds=5), phases=phases)
def test_reactor_client_async_server(data):
    _TestRunner(
        data,
        Config(
            (2, 3),
            (2, 3),
            run_mode=RunMode.ASYNC_SERVER,
            client_run_modes=[RunMode.REACTOR_CLIENT],
            port=9021,
        ),
    ).run()


# Asynchronous server, two reactor clients
@given(data=data())
@settings(deadline=timedelta(seconds=5), phases=phases)
def test_two_reactor_clients_async_server(data):
    _TestRunner(
        data,
        Config(
            (4, 5),
            (4, 5),
            run_mode=RunMode.ASYNC_SERVER,
            client_run_modes=[RunMode.REACTOR_CLIENT, RunMode.REACTOR_CLIENT],
            port=9022,
        ),
    ).run()


# Reactor server, one reactor client
@given(data=data())
@settings(deadline=timedelta(seconds=5), phases=phases)
def test_reactor_client_reactor_server(data):
    _TestRunner(
        data,
        Config(
            (2, 3),
            (2, 3),
            run_mode=RunMode.REACTOR_SERVER,
            client_run_modes=[RunMode.REACTOR_CLIENT],
            port=9023,
        ),
    ).run()


# Synchronous server, two reactor clients
@given(data=data())
@settings(deadline=timedelta(seconds=5), phases=phases)
def test_two_reactor_clients_reactor_server(data):
    _TestRunner(
        data,
        Config(
            (4, 5),
            (4, 5),
            run_mode=RunMode.REACTOR_SERVER,
            client_run_modes=[RunMode.REACTOR_CLIENT, RunMode.REACTOR_CLIENT],
            port=9024,
        ),
    ).run()


# TODO: Ultimate test - 4 clients, 1 server and long password?
