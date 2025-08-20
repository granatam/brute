from datetime import timedelta

import pytest
from hypothesis import given, settings
from hypothesis.strategies import data
from testrunner import Config, RunMode, _TestRunner, phases


@pytest.mark.parametrize(
    "run_mode", [RunMode.SINGLE, RunMode.MULTI, RunMode.GENERATOR]
)
@pytest.mark.parametrize("bounds", [(2, 6), (5, 5)])
@given(data=data())
@settings(deadline=timedelta(seconds=3), phases=phases)
def test_simple(data, run_mode, bounds):
    _TestRunner(data, Config(bounds, bounds, run_mode=run_mode)).run()
