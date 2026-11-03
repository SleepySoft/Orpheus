"""直接型一阶/四阶/最高阶IIR的冻结、动态系数清零及四入口验收。"""
from functools import partial

from test_iir_freeze import BLOCKS, verify_iir_freeze_cases


def direct_reference(samples, params, *, coefficient_updates=False):
    channels, order = params["channels"], params["order"]
    flat = [float(v) for v in params["coefs"].split(",")]
    history = [[0.0]*order for _ in range(channels)]
    expected, cursor = [], 0
    for step, blocks in enumerate(BLOCKS):
        for n in range(cursor, cursor+32*blocks):
            for channel in range(channels):
                if step % 2 and not coefficient_updates:
                    expected.append(0)
                    continue
                offset = channel*(2*order+1) if params.get("coefs_mode") == "per_channel" else 0
                c = flat[offset:offset+2*order+1]
                if step % 2 and coefficient_updates:
                    c = [0.0]*len(c)
                x = samples[n][channel]/32768
                z = history[channel]
                y = c[0]*x+z[0]
                for k in range(order-1):
                    z[k] = (z[k+1]+c[k+1]*x)-c[order+k+1]*y
                z[-1] = c[order]*x-c[2*order]*y
                expected.append(int(y*32767))
        cursor += 32*blocks
    return expected


def direct_cases():
    per_channel = []
    for channel in range(3):
        per_channel.extend([(channel+1)/8, 0.0625, 0, 0, 0, -0.25, 0, 0, 0])
    return {
        "first_order": {"channels": 22, "order": 1, "coefs": "0.5,0.25,-0.5"},
        "fourth_order": {"channels": 3, "order": 4, "coefs_mode": "per_channel",
                         "coefs": ",".join(map(repr, per_channel))},
        "max_order": {"channels": 64, "order": 16,
                      "coefs": ",".join(map(repr, [0.5]+[0.01]*16+[-0.25]+[0.0]*15))},
    }


def verify_direct_cases(tmp_path, cases, *, coefficient_updates=False):
    verify_iir_freeze_cases(
        tmp_path, cases, component="orpheus.builtin.iir", bulk_stride=33,
        reference=partial(direct_reference, coefficient_updates=coefficient_updates),
        coefficient_updates=coefficient_updates)


def test_direct_iir_freeze_four_entries(tmp_path):
    verify_direct_cases(tmp_path, direct_cases())


def test_direct_iir_coefficient_zero_four_entries(tmp_path):
    verify_direct_cases(tmp_path, direct_cases(), coefficient_updates=True)
