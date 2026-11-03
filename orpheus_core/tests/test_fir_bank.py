"""FIR组独立卷积参考、暂停与系数清零/恢复的四入口验收。"""
import pytest

from test_iir_freeze import BLOCKS, verify_iir_freeze_cases


def fir_reference(samples, params, *, coefficient_updates=False):
    channels, taps = params["channels"], params["num_taps"]
    coefs = [float(v) for v in params["coefs"].split(",")]
    counts = ([int(v) for v in params["taps_per_channel"].split(",")]
              if params.get("taps_per_channel") else [taps]*channels)
    history = [[] for _ in range(channels)]
    result, cursor = [], 0
    for step, blocks in enumerate(BLOCKS):
        for row in samples[cursor:cursor+32*blocks]:
            for c in range(channels):
                if step % 2 and not coefficient_updates:
                    result.append(0)
                    continue
                history[c].insert(0, row[c]/32768)
                del history[c][counts[c]:]
                start = c*taps if params.get("coefs_mode") == "per_channel" else 0
                value = sum(x*coefs[start+k] for k, x in enumerate(history[c]))
                result.append(0 if step % 2 and coefficient_updates else int(value*32767))
        cursor += 32*blocks
    return result


@pytest.mark.parametrize("coefficient_updates", [False, True])
def test_fir_bank_four_entries(tmp_path, coefficient_updates):
    cases = {
        "shared": {"channels": 3, "num_taps": 3, "coefs": "0.5,0.25,0.125",
                   "taps_per_channel": "0,1,3"},
        "separate": {"channels": 64, "num_taps": 3, "coefs_mode": "per_channel",
                     "coefs": ",".join(str(v) for c in range(64)
                                       for v in ((c+1)/256, -0.125, 0.0625))},
    }
    verify_iir_freeze_cases(
        tmp_path, cases, component="orpheus.builtin.fir_bank", bulk_stride=3,
        coefficient_updates=coefficient_updates,
        reference=lambda samples, params: fir_reference(
            samples, params, coefficient_updates=coefficient_updates))
