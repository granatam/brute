import time

from hypothesis import given, settings
from hypothesis.strategies import data
from testrunner import (
    DEFAULT_PORT,
    CommandMode,
    Config,
    RunMode,
    _TestRunner,
    phases,
)

MAX_RUNS = 1000
SKIP = 10


def run_perf_test(data, run_mode, client_run_modes, port=DEFAULT_PORT):
    # First example is alphabet and password that contains only A's, so
    # we need to skip some generated examples
    for i in range(SKIP):
        testrunner = _TestRunner(
            data,
            Config(
                (7, 7),
                (10, 10),
                run_mode=run_mode,
                client_run_modes=client_run_modes,
                port=port,
            ),
        )
    start = time.time()
    for i in range(MAX_RUNS):
        testrunner.run(data, CommandMode.PERF)
    end = time.time()
    print(f"{MAX_RUNS} runs in {run_mode}: {end - start:.4f} seconds")


@given(data=data())
@settings(deadline=None, phases=phases, max_examples=1)
def test_performance_single(data):
    run_perf_test(data, RunMode.SINGLE, [])


@given(data=data())
@settings(deadline=None, phases=phases, max_examples=1)
def test_performance_multi(data):
    run_perf_test(data, RunMode.MULTI, [])


@given(data=data())
@settings(deadline=None, phases=phases, max_examples=1)
def test_performance_gen(data):
    run_perf_test(data, RunMode.GENERATOR, [])
