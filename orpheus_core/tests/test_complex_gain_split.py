"""实增益作用于复数，每次交付保留先乘后减的两次舍入。"""

import struct

import pytest

from frequency_interface_fixtures import frequency_graph, gain_values, source_values
from test_table_interfaces import bits, f32


def verify_complex_gain_split(tmp_path, *, channels=1, elements=129, step=128, reference=None):
    impulse, reverb = [], []
    for call in range(4):
        values = source_values(channels, elements, call)
        gains = gain_values(channels, elements, call)
        if reference:
            original = struct.pack(f"<{len(values)}f", *values)
            gain = struct.pack(f"<{len(gains)}f", *gains)
            residual, scaled = reference(original, gain)
            impulse.append(list(struct.unpack(f"<{len(residual)//4}I", residual)))
            reverb.append(list(struct.unpack(f"<{len(scaled)//4}I", scaled)))
        else:
            scaled = [f32(gains[(i // (channels * 2)) * channels + i % channels] * value)
                      for i, value in enumerate(values)]
            impulse.append([bits(f32(a - b)) for a, b in zip(values, scaled)])
            reverb.append([bits(value) for value in scaled])
    return frequency_graph(tmp_path, "complex_gain_split", {"channels": channels, "elements": elements},
                           (elements * 2, elements * 2), (impulse, reverb), gain=True, step=step)


@pytest.mark.parametrize("channels,elements,step", [(1, 129, 128), (1, 387, 384), (3, 7, 32)])
def test_complex_gain_split_four_entries(tmp_path, channels, elements, step):
    verify_complex_gain_split(tmp_path, channels=channels, elements=elements, step=step)
