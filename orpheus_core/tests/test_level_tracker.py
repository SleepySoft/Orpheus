"""同周期双dB统计→保持衰减→声压级/包络，覆盖四入口。"""
from test_block_stats import verify_block_stats


def test_level_tracker_four_entries(tmp_path):
    cases = {"slow": 10, "fast": 2}
    trackers = {
        name: {
            "min_db": -100.0, "max_db": 0.0, "prediction_db_spl": 100.0,
            "hold_margin_db": 0.0, "hold_calls": 1.0 if name == "slow" else 0.0,
            "slow_decay_db": -.25, "fast_decay_db": -1.0, "fast_multiplier": 3.0,
            "trigger_threshold": .5,
        } for name in cases
    }
    verify_block_stats(tmp_path, cases, trackers=trackers)
