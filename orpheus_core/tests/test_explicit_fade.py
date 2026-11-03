"""外部分频的成对包络：只由高频计数驱动，四入口状态逐位核对。"""
import math

from test_phased_tone import f32
from test_sample_gain import verify_sample_gains


def reference_fade(name, samples, params, state):
    channels = params["channels"]
    mapping = [int(v) for v in params["chan_map"].split(",")]
    starts, coefficients = list(state["current_gains"]), [1.]*4
    for r in range(2):
        if not state["freeze"]:
            if not state["remaining_blocks"][r]:
                starts[r] = state["target_gains"][r]
                starts[r+2] = state["target_gains"][r+2]
            else:
                state["remaining_blocks"][r] = max(-2147483648, state["remaining_blocks"][r]-1)
                coefficients[r], coefficients[r+2] = state["coefficients"][r], state["coefficients"][r+2]
    output = []
    for n, row in enumerate(samples):
        gains = [f32(g*f32(math.pow(k, n+1))) for g, k in zip(starts, coefficients)]
        for c, r in enumerate(mapping):
            x, low = f32(row[c]/32768), f32(row[channels+c]/32768)
            y = 0. if r == -1 else f32(f32(low*gains[r+2])+f32(f32(x-low)*gains[r]))
            output.append(int(max(-1, min(1, y))*32767))
    state["current_gains"] = gains
    return output


def test_explicit_fade_four_entries(tmp_path):
    verify_sample_gains(tmp_path, {
        "fade": {"channels": 22, "num_rampers": 4, "execution_mode": 1,
                 "chan_map": ",".join(str(c % 2) if c < 20 else "-1" for c in range(22)),
                 "current_gains": ".8,.7,.6,.5", "target_gains": ".3,.2,.1,.4",
                 "coefficients": ".995,.99,1.005,1.01", "remaining_blocks": "1,0,77,-123"},
        "unused": {"channels": 4, "num_rampers": 4, "execution_mode": 1, "chan_map": "-1,-1,-1,-1"},
    }, reference=reference_fade, component="fade", input_ports=("in", "lpf0"))
