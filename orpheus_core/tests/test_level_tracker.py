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


def test_same_block_volume_gain_four_entries(tmp_path):
    cases = {"volume": 10}
    trackers = {"volume": {
        "min_db": -80.0, "max_db": 0.0, "prediction_db_spl": 123.0,
        "hold_margin_db": 6.0, "hold_calls": 4500.0,
        "slow_decay_db": -.0000666666674, "fast_decay_db": -.00333333341,
        "fast_multiplier": 3.0, "trigger_threshold": .5,
    }}
    gains = {"volume": {
        "channels": 10, "num_rampers": 1, "execution_mode": 1, "chan_map": ",".join(["0"]*10),
        "current_gains": "0.04", "target_gains": "0.2", "coefficients": "1.005",
        "remaining_blocks": "2", "gain_output": 1, "gain_channel": 0,
    }}
    verify_block_stats(tmp_path, cases, trackers=trackers, volume_gains=gains)
