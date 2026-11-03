"""40/42/64通道IIR、索引路由和WAV的四入口高位通道隔离。"""
from test_iir_freeze import verify_iir_freeze_cases


def test_wide_iir_four_entries(tmp_path):
    cases = {"legacy": {"channels": 2, "num_stages": 1, "coefs": "0.5,0.25,0,-0.5,0"}}
    for count in (40, 42, 64):
        coefs = []
        for channel in range(count):
            coefs.extend([1/(channel+2), 0.125, 0, -0.25, 0.125, 0.5, 0, 0, -0.125, 0])
        cases[f"wide_{count}"] = {
            "channels": count, "num_stages": 2, "coefs_mode": "per_channel",
            "stages_per_channel": ",".join(str(c % 3) for c in range(count)),
            "coefs": ",".join(map(repr, coefs)),
        }
    verify_iir_freeze_cases(tmp_path, cases)
