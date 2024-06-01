# from datetime import timedelta
# import string
# from hypothesis import given, strategies as st, settings
# from utils import (
#     shuffle_password,
#     run_client_server,
#     run_two_clients_server,
#     capture_client_server_log,
#     phases,
# )


# # Synchronous server, one synchronous client
# @given(
#     st.text(min_size=3, alphabet=string.ascii_letters, max_size=4),
#     st.text(min_size=1, max_size=1, alphabet="iry"),
# )
# @settings(deadline=timedelta(seconds=5), phases=phases)
# def test_sync_client_sync_server(passwd, brute_mode):
#     alph = shuffle_password(passwd)
#     capture_client_server_log(
#         passwd, alph, brute_mode, "c", "S", run_client_server
#     )


# # Synchronous server, two synchronous clients
# @given(
#     st.text(min_size=4, alphabet=string.ascii_letters, max_size=5),
#     st.text(min_size=1, max_size=1, alphabet="iry"),
# )
# @settings(deadline=timedelta(seconds=5), phases=phases)
# def test_two_sync_clients_sync_server(passwd, brute_mode):
#     alph = shuffle_password(passwd)
#     capture_client_server_log(
#         passwd, alph, brute_mode, "c", "S", run_two_clients_server, 2
#     )


# # Asynchronous server, one synchronous client
# @given(
#     st.text(min_size=3, alphabet=string.ascii_letters, max_size=4),
#     st.text(min_size=1, max_size=1, alphabet="iry"),
# )
# @settings(deadline=timedelta(seconds=5), phases=phases)
# def test_sync_client_async_server(passwd, brute_mode):
#     alph = shuffle_password(passwd)
#     capture_client_server_log(
#         passwd, alph, brute_mode, "c", "w", run_client_server
#     )


# # Asynchronous server, two synchronous clients
# @given(
#     st.text(min_size=4, alphabet=string.ascii_letters, max_size=5),
#     st.text(min_size=1, max_size=1, alphabet="iry"),
# )
# @settings(deadline=timedelta(seconds=5), phases=phases)
# def test_two_sync_clients_async_server(passwd, brute_mode):
#     alph = shuffle_password(passwd)
#     capture_client_server_log(
#         passwd, alph, brute_mode, "c", "w", run_two_clients_server, 2
#     )


# # Asynchronous server, one asynchronous client
# @given(
#     st.text(min_size=3, alphabet=string.ascii_letters, max_size=4),
#     st.text(min_size=1, max_size=1, alphabet="iry"),
# )
# @settings(deadline=timedelta(seconds=5), phases=phases)
# def test_async_client_async_server(passwd, brute_mode):
#     alph = shuffle_password(passwd)
#     capture_client_server_log(
#         passwd, alph, brute_mode, "v", "w", run_client_server
#     )


# # Asynchronous server, two asynchronous clients
# @given(
#     st.text(min_size=4, alphabet=string.ascii_letters, max_size=5),
#     st.text(min_size=1, max_size=1, alphabet="iry"),
# )
# @settings(deadline=timedelta(seconds=5), phases=phases)
# def test_two_async_clients_async_server(passwd, brute_mode):
#     alph = shuffle_password(passwd)
#     capture_client_server_log(
#         passwd, alph, brute_mode, "v", "w", run_two_clients_server, 2
#     )


# # Synchronous server, one asynchronous client
# @given(
#     st.text(min_size=3, alphabet=string.ascii_letters, max_size=4),
#     st.text(min_size=1, max_size=1, alphabet="iry"),
# )
# @settings(deadline=timedelta(seconds=5), phases=phases)
# def test_async_client_sync_server(passwd, brute_mode):
#     alph = shuffle_password(passwd)
#     capture_client_server_log(
#         passwd, alph, brute_mode, "v", "w", run_client_server
#     )


# # Synchronous server, two asynchronous clients
# @given(
#     st.text(min_size=4, alphabet=string.ascii_letters, max_size=5),
#     st.text(min_size=1, max_size=1, alphabet="iry"),
# )
# @settings(deadline=timedelta(seconds=5), phases=phases)
# def test_two_async_clients_sync_server(passwd, brute_mode):
#     alph = shuffle_password(passwd)
#     capture_client_server_log(
#         passwd, alph, brute_mode, "v", "S", run_two_clients_server, 2
#     )
