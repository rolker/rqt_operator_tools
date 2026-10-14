"""Keep-latest-per-area behaviour, including the latched replay."""

from fake_msgs import assessment, source
from rqt_coverage.score_model import REPLAY_WINDOW_S, AreaScore, Store


def score(area_id='a', sec=100, covered=10.0, **kw):
    return AreaScore.from_msg(
        assessment(area_id, total=100.0, covered=covered, meeting=0.0, sec=sec, **kw))


def test_replay_of_100_repeats_converges_to_newest():
    store = Store()
    # Late joiner: 100 messages, oldest first, five repeats of each of 20 areas.
    for i in range(100):
        store.update(score(f'area{i % 20}', sec=i, covered=float(i)), now=1.0)
    assert len(store) == 20
    for k in range(20):
        newest = 80 + k
        assert store.get(f'area{k}').score.stamp_ns == newest * 1_000_000_000
        assert store.get(f'area{k}').score.covered == float(newest)


def test_out_of_order_older_stamp_is_dropped_inside_the_window():
    store = Store()
    assert store.update(score(sec=50, covered=50.0), now=10.0)
    assert not store.update(score(sec=40, covered=40.0), now=10.5)
    assert store.get('a').score.covered == 50.0
    assert store.get('a').received == 10.0


def test_backwards_stamp_after_the_window_is_a_clock_restart():
    store = Store()
    store.update(score(sec=5000, covered=50.0), now=10.0)
    assert store.update(score(sec=3, covered=7.0), now=10.0 + REPLAY_WINDOW_S + 1)
    assert store.get('a').score.covered == 7.0


def test_equal_stamp_last_received_wins():
    store = Store()
    store.update(score(sec=5, covered=1.0), now=1.0)
    assert store.update(score(sec=5, covered=2.0), now=2.0)
    assert store.get('a').score.covered == 2.0


def test_areas_coexist_and_ids_are_sorted():
    store = Store()
    store.update(score('b'), now=1.0)
    store.update(score('a'), now=1.0)
    assert store.ids() == ['a', 'b']


def test_forget_and_clear():
    store = Store()
    store.update(score('a'), now=1.0)
    store.update(score('b'), now=1.0)
    store.forget('a')
    store.forget('missing')   # no error
    assert store.ids() == ['b']
    store.clear()
    assert len(store) == 0


def test_source_list_may_change_shape_between_messages():
    store = Store()
    store.update(score(sec=1, sources=[source('b1', 1, 0, 0), source('b2', 1, 0, 0)]),
                 now=1.0)
    store.update(score(sec=2, sources=[source('b2', 5, 0, 0)]), now=2.0)
    assert [s.source for s in store.get('a').score.sources] == ['b2']
