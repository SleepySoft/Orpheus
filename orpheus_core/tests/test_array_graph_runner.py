import copy

import pytest

from array_graph_runner import execute_array_graph, use_linear_call_chain
from orpheus_core.compiler import ExecutionPlan


def plan_for(periods):
    nodes = list(periods)
    return ExecutionPlan(
        abi_version=6, sample_rate=48000, block_size=32, task_id="default",
        execution_order=nodes, schedule={"tick": 32, "periods": periods},
        tasks=[{"nodes": nodes, "entry_nodes": []}],
    )


def test_single_cadence_keeps_one_component_chain():
    plan = plan_for({"source": 1, "a": 1, "b": 1})
    use_linear_call_chain(plan)
    assert plan.entry_nodes == plan.tasks[0]["entry_nodes"] == ["source"]
    assert plan.call_graph == {
        "source": [{"node": "a", "every": 1, "phase": 0}],
        "a": [{"node": "b", "every": 1, "phase": 0}], "b": [],
    }


def test_slow_consumer_and_independent_fast_segment_have_explicit_source_calls():
    plan = plan_for({"source": 1, "batch": 1, "slow": 2, "slow2": 2, "fast": 1, "fast2": 1})
    use_linear_call_chain(plan)
    assert plan.call_graph == {
        "source": [{"node": "batch", "every": 1, "phase": 0},
                   {"node": "slow", "every": 2, "phase": 1},
                   {"node": "fast", "every": 1, "phase": 0}],
        "batch": [], "slow": [{"node": "slow2", "every": 1, "phase": 0}], "slow2": [],
        "fast": [{"node": "fast2", "every": 1, "phase": 0}], "fast2": [],
    }


def test_existing_explicit_component_branches_are_not_rewritten():
    plan = plan_for({"source": 1, "a": 1, "b": 1})
    plan.flow_mode = "task_sequence"
    plan.entry_nodes = ["source"]
    plan.call_graph = {
        "source": [{"node": "a", "output_port": 7, "input_port": 1, "every": 3, "phase": 0},
                   {"node": "b", "output_port": 7, "input_port": 0, "every": 1, "phase": 0}],
        "a": [], "b": [],
    }
    before = copy.deepcopy(plan)
    use_linear_call_chain(plan)
    assert plan == before


def test_numeric_fixture_without_base_cadence_fails_explicitly():
    with pytest.raises(ValueError, match="基本节拍"):
        use_linear_call_chain(plan_for({"source": 2, "a": 2}))


def test_explicit_multitask_capture_fails_before_generation(tmp_path):
    plan = plan_for({"source": 1})
    plan.flow_mode = "task_sequence"
    plan.tasks.append({"id": "other", "nodes": [], "entry_nodes": []})
    with pytest.raises(ValueError, match="一个Task"):
        execute_array_graph(tmp_path, None, plan, {})


@pytest.mark.parametrize("calls", [0, -1, 1025, 1.5, True, "2"])
def test_invalid_capture_cadence_fails_before_generation(tmp_path, calls):
    with pytest.raises(ValueError, match="调用次数"):
        execute_array_graph(tmp_path, None, None, {}, calls_per_block=calls)


@pytest.mark.parametrize("calls", [
    {}, {"wrong": 2}, {"capture": 2, "extra": 1}, {"capture": 0},
    {"capture": True}, {"capture": 2.5}, {"capture": 1025},
])
def test_invalid_per_capture_cadence_fails_before_generation(tmp_path, calls):
    with pytest.raises(ValueError, match="调用次数"):
        execute_array_graph(tmp_path, None, None, {"capture": (1, 1)}, calls_per_block=calls)
