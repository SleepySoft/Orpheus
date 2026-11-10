"""复数功率的实际交付点、逐批归约和独立数值预期。"""

import pytest

from frequency_interface_fixtures import frequency_graph, power_expected


def verify_complex_power(tmp_path, *, channels=1, bins=129, batches=1, step=128, reference=None):
    return frequency_graph(tmp_path, "complex_power",
                           {"channels": channels, "bins": bins, "batches": batches},
                           (bins * batches, batches), power_expected(channels, bins, batches, reference),
                           step=step, approximate_energy=reference is None)


@pytest.mark.parametrize("channels,bins,batches,step", [(1, 129, 1, 128), (1, 129, 3, 384), (3, 7, 2, 32)])
def test_complex_power_four_entries(tmp_path, channels, bins, batches, step):
    verify_complex_power(tmp_path, channels=channels, bins=bins, batches=batches, step=step)
