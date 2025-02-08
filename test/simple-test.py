from datetime import timedelta

from hypothesis import given, settings
from hypothesis.strategies import data
from testrunner import Config, RunMode, _TestRunner, phases


@given(data=data())
@settings(deadline=timedelta(seconds=3), phases=phases)
def test_single_short_password(data):
    _TestRunner(Config((2, 6), (2, 6), run_mode=RunMode.SINGLE)).run(data)


@given(data=data())
@settings(deadline=timedelta(seconds=3), phases=phases)
def test_multi_short_password(data):
    _TestRunner(Config((2, 6), (2, 6), run_mode=RunMode.MULTI)).run(data)


@given(data=data())
@settings(deadline=timedelta(seconds=3), phases=phases)
def test_gen_short_password(data):
    _TestRunner(Config((2, 6), (2, 6), run_mode=RunMode.GENERATOR)).run(data)


@given(data=data())
@settings(deadline=timedelta(seconds=3), phases=phases, max_examples=20)
def test_single_corner_cases(data):
    _TestRunner(Config((5, 5), (5, 5), run_mode=RunMode.SINGLE)).run(data)


@given(data=data())
@settings(deadline=timedelta(seconds=3), phases=phases, max_examples=20)
def test_multi_corner_cases(data):
    _TestRunner(Config((5, 5), (5, 5), run_mode=RunMode.MULTI)).run(data)


@given(data=data())
@settings(deadline=timedelta(seconds=3), phases=phases, max_examples=20)
def test_gen_corner_cases(data):
    _TestRunner(Config((5, 5), (5, 5), run_mode=RunMode.GENERATOR)).run(data)
