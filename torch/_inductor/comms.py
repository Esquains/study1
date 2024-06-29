# mypy: allow-untyped-defs
# pyre-strict
from __future__ import annotations

import operator

from collections import defaultdict
from typing import Dict, List, Set, Tuple, TYPE_CHECKING

import torch

from . import config, ir
from .dependencies import WeakDep
from .pattern_matcher import (
    CallFunction,
    KeywordArg,
    Match,
    PatternMatcherPass,
    register_graph_pattern,
)
from .utils import is_collective, is_wait, tuple_sorted

overlap_log = torch._logging.getArtifactLogger(__name__, "overlap")

if TYPE_CHECKING:
    from .scheduler import BaseSchedulerNode


def reinplace_fsdp_all_gather(graph: torch.fx.Graph) -> None:
    """
    # File: torch/distributed/_composable/fsdp/_fsdp_collectives.py:152 in foreach_all_gather, code: all_gather_input, all_gather_output = torch.ops.fsdp.all_gather_copy_in(
    all_gather_copy_in = torch.ops.fsdp.all_gather_copy_in.default([primals_2, primals_3, primals_4, primals_5], [64, 128, 8, 8], 208, 2, 0, torch.float32, device(type='cuda', index=0));
    getitem: "f32[208][1]cuda:0" = all_gather_copy_in[0];

    # File: torch/distributed/_functional_collectives.py:204 in all_gather_tensor, code: tensor = torch.ops._c10d_functional.all_gather_into_tensor(
    all_gather_into_tensor: "f32[416][1]cuda:0" = torch.ops._c10d_functional.all_gather_into_tensor.default(getitem, 2, '0');

    ->

    all_gather_copy_in = torch.ops.fsdp.all_gather_copy_in.default([primals_2, primals_3, primals_4, primals_5], [64, 128, 8, 8], 208, 2, 0, torch.float32, device(type='cuda', index=0));
    getitem: "f32[208][1]cuda:0" = all_gather_copy_in[0];
    getitem_1: "f32[416][1]cuda:0" = all_gather_copy_in[1];

    all_gather_into_tensor: "f32[416][1]cuda:0" = torch.ops._c10d_functional.all_gather_into_tensor_out.default(getitem, 2, '0', out=getitem_1);
    """

    def remove_unused_getitem(g):
        # Remove `getitem_X = all_gather_copy_in[1]` which is never used.
        node_list = list(g.nodes)
        for n in node_list:
            if (
                n.target == operator.getitem
                and n.args[0].target is torch.ops.fsdp.all_gather_copy_in.default
                and n.args[1] == 1
            ):
                g.erase_node(n)

    graph_pass = PatternMatcherPass()

    @register_graph_pattern(
        CallFunction(
            torch.ops._c10d_functional.all_gather_into_tensor.default,
            CallFunction(
                operator.getitem,
                CallFunction(
                    torch.ops.fsdp.all_gather_copy_in.default,
                    KeywordArg("all_gather_inputs"),
                    KeywordArg("inp_split_sizes"),
                    KeywordArg("all_gather_input_numel"),
                    KeywordArg("world_size"),
                    KeywordArg("rank"),
                    KeywordArg("dtype"),
                    KeywordArg("device"),
                ),
                KeywordArg("item_idx"),
            ),
            KeywordArg("group_size"),
            KeywordArg("group_name"),
        ),
        pass_dict=graph_pass,
        extra_check=lambda match: match.kwargs["item_idx"] == 0,
    )
    def reinplace_all_gather(match: Match, *args, **kwargs):
        all_gather_inputs = kwargs["all_gather_inputs"]
        inp_split_sizes = kwargs["inp_split_sizes"]
        all_gather_input_numel = kwargs["all_gather_input_numel"]
        world_size = kwargs["world_size"]
        rank = kwargs["rank"]
        dtype = kwargs["dtype"]
        device = kwargs["device"]
        group_size = kwargs["group_size"]
        group_name = kwargs["group_name"]

        def repl(
            all_gather_inputs,
            inp_split_sizes,
            all_gather_input_numel,
            world_size,
            rank,
            dtype,
            device,
            group_size,
            group_name,
        ):
            all_gather_copy_in = torch.ops.fsdp.all_gather_copy_in.default(
                all_gather_inputs,
                inp_split_sizes,
                all_gather_input_numel,
                world_size,
                rank,
                dtype,
                device,
            )
            getitem = all_gather_copy_in[0]
            getitem_1 = all_gather_copy_in[1]
            all_gather_into_tensor = (
                torch.ops._c10d_functional.all_gather_into_tensor_out.default(
                    getitem, group_size, group_name, out=getitem_1
                )
            )
            return all_gather_into_tensor

        match.replace_by_example(
            repl,
            [
                all_gather_inputs,
                inp_split_sizes,
                all_gather_input_numel,
                world_size,
                rank,
                dtype,
                device,
                group_size,
                group_name,
            ],
        )

        # TODO: maybe you want to replace downstream usage of AGO with all_gather_copy_in[1]

    remove_unused_getitem(graph)
    graph_pass.apply(graph)  # type: ignore[arg-type]


"""
        node_list = list(graph.nodes)
        for i, n in enumerate(node_list):
            if n.target == torch.ops.fsdp.all_gather_copy_in.default:
                all_gather_copy_in_node = n
                getitem_0_node = node_list[i+1]
                assert getitem_0_node.target == operator.getitem and getitem_0_node.args[0] == all_gather_copy_in_node
                all_gather_node_start_index = None
                for k in range(i+3, len(node_list)):
                    if node_list[k].target is torch.ops._c10d_functional.all_gather_into_tensor.default and node_list[k].args[0] == getitem_0_node:
                        all_gather_node_start_index = k
                        break
                if all_gather_node_start_index is None:
                    continue
                all_gather_node = node_list[all_gather_node_start_index]
                with graph.inserting_before(all_gather_node):
                    getitem_1_node = graph.call_function(operator.getitem, (all_gather_copy_in_node, 1))
                with graph.inserting_before(all_gather_node):
                    inplace_all_gather_node = graph.call_function(torch.ops._c10d_functional.all_gather_into_tensor_out.default, (getitem_0_node, *all_gather_node.args[1:]), {"out": getitem_1_node})
                all_gather_node.replace_all_uses_with(inplace_all_gather_node, propagate_meta=True)
                graph.erase_node(all_gather_node)
"""


def sink_waits(
    snodes: List[BaseSchedulerNode],
    node_users: Dict[BaseSchedulerNode, Set[BaseSchedulerNode]],
    inverse_users: Dict[BaseSchedulerNode, Set[BaseSchedulerNode]],
) -> List[BaseSchedulerNode]:
    """
    Greedily moves waits as late as possible (i.e. until we reach a use). Optimal in terms of
    communication overlap.
    """
    new_order = []
    cur_waits = set()
    for snode in snodes:
        if is_wait(snode.node):
            cur_waits.add(snode)
        else:
            for wait in tuple_sorted(cur_waits):
                if snode in node_users[wait]:
                    new_order.append(wait)
                    cur_waits.remove(wait)
            new_order.append(snode)
    new_order.extend(tuple_sorted(cur_waits))
    return new_order


def raise_comms(
    snodes: List[BaseSchedulerNode],
    node_users: Dict[BaseSchedulerNode, Set[BaseSchedulerNode]],
    inverse_users: Dict[BaseSchedulerNode, Set[BaseSchedulerNode]],
) -> List[BaseSchedulerNode]:
    """
    Greedily moves comms as early as possible (i.e. until we reach an input).
    Optimal in terms of communication overlap.

    TODO: We might want to adjust this in the future to account for memory limitations.
    e.g. when we are compiling FSDP, this heuristics will cause the all-gathers to be prefetched as soon as possible,
    which is the beginning of the forwards pass. We'll have to either do a special pass for FSDP,
    or we'll want to redo this pass with memory considerations so we handle the FSDP case in a general way.
    """
    new_order_reversed: List[BaseSchedulerNode] = []
    cur_comms: List[BaseSchedulerNode] = []
    for snode in reversed(snodes):
        if is_collective(snode.node):
            cur_comms.append(snode)
        else:
            for comm in cur_comms:
                assert len(inverse_users[comm]) > 0
            while len(cur_comms) > 0 and any(
                snode in inverse_users[comm] for comm in cur_comms
            ):
                comm = cur_comms.pop(0)
                new_order_reversed.append(comm)
            new_order_reversed.append(snode)
    assert len(cur_comms) <= 1
    new_order_reversed.extend(tuple_sorted(cur_comms))
    return new_order_reversed[::-1]


def get_ancestors(node, inverse_users):
    ancestors = set()
    cur_nodes = [node]
    while len(cur_nodes) > 0:
        new_nodes = []
        for node in cur_nodes:
            for inp in inverse_users[node]:
                if inp not in ancestors:
                    ancestors.add(inp)
                    new_nodes.append(inp)
        cur_nodes = new_nodes
    return ancestors


def get_descendants(node, node_users):
    descendants = set()
    cur_nodes = [node]
    while len(cur_nodes) > 0:
        new_nodes = []
        for node in cur_nodes:
            for inp in node_users[node]:
                if inp not in descendants:
                    descendants.add(inp)
                    new_nodes.append(inp)
        cur_nodes = new_nodes
    return descendants


def decide_global_ordering_of_comms(nodes: List[BaseSchedulerNode]):
    """
    Decide global ordering of comms, by just enforcing the ordering that's in the input graph
    (might not be the same ordering as the eager mode program).
    TODO: Come up with a better approach
    """
    comm_nodes = [n for n in nodes if is_collective(n.node)]
    for i in range(1, len(comm_nodes)):
        # Enforce ordering by making previous comm a `WeakDep` dependency of the next comm
        comm_nodes[i].add_fake_dep(WeakDep(comm_nodes[i - 1].get_name()))


def assert_no_comm_nodes(snodes: List[BaseSchedulerNode]) -> None:
    assert not any(is_collective(snode.node) for snode in snodes)


def estimate_op_runtime(snode: BaseSchedulerNode) -> float:
    """
    Returns estimated op runtime in nanoseconds (ns)
    """
    if config.estimate_op_runtime == "default":
        runtime = snode.get_estimated_runtime()
    else:
        assert callable(config.estimate_op_runtime)
        runtime = config.estimate_op_runtime(snode)
    return runtime


def compute_node_users(
    snodes: List[BaseSchedulerNode],
) -> Tuple[
    Dict[BaseSchedulerNode, Set[BaseSchedulerNode]],
    Dict[BaseSchedulerNode, Set[BaseSchedulerNode]],
]:
    from .scheduler import FusedSchedulerNode

    # set up buffer name to (fused)snode mapping
    buf_to_snode: Dict[str, BaseSchedulerNode] = {}
    for node in snodes:
        if isinstance(node, FusedSchedulerNode):
            for x in node.snodes:
                buf_to_snode[x.get_name()] = node
        buf_to_snode[node.get_name()] = node

    # compute inverse_users
    inverse_users = {
        node: {buf_to_snode[dep.name] for dep in node.unmet_dependencies}
        for node in snodes
    }

    # compute node_users
    # TODO: ideally, we should deduplicate .users and .node_users,
    # but currently .users contains extra information that's difficult to
    # extract into a standalone container.
    node_users: Dict[BaseSchedulerNode, Set[BaseSchedulerNode]] = defaultdict(set)
    for node, node_inverse_users in inverse_users.items():
        for inverse_user in node_inverse_users:
            node_users[inverse_user].add(node)

    return inverse_users, node_users


def reorder_compute_for_overlap(
    snodes: List[BaseSchedulerNode],
    node_users: Dict[BaseSchedulerNode, Set[BaseSchedulerNode]],
    inverse_users: Dict[BaseSchedulerNode, Set[BaseSchedulerNode]],
) -> List[BaseSchedulerNode]:
    """
    Decides a global ordering of all compute and communication nodes,
    assuming that we already have a global ordering of communication nodes.

    Overall scheduling procedure is:
        Step 1: Given that we've currently scheduled comm N, we now schedule all compute nodes
            that are required for comm N + 1 but do not depend on comm N, to run at the same time with comm N.
        Step 2: If all those compute nodes are sufficient to overlap comm N, we're done.
            Otherwise, we now need to look elsewhere to find compute that overlaps with comm N.
            We prioritize compute nodes that are needed sooner.
        Step 3: We schedule the compute nodes dependent on comm N and required for comm N + 1.
        Step 4: We schedule comm N + 1.
        Repeat this for subsequent comm nodes.
    """
    final_order = []

    comm_nodes = []
    for snode in snodes:
        if is_collective(snode.node):
            comm_nodes.append(snode)
    if len(comm_nodes) == 0:
        # if there is no comm nodes, return the current order
        return snodes

    comm_ancestors = {node: get_ancestors(node, inverse_users) for node in comm_nodes}
    comm_descendants = {node: get_descendants(node, node_users) for node in comm_nodes}

    indeg = dict.fromkeys(snodes, 0)
    for snode in snodes:
        for user in node_users[snode]:
            if user in indeg:
                indeg[user] += 1
    ready_to_schedule_nodes = {node for node in snodes if indeg[node] == 0}

    unscheduled_nodes = set()
    unscheduled_nodes = set(snodes)

    def schedule_node(snode):
        """
        Schedule a single node.
        """
        assert snode in unscheduled_nodes
        assert snode in ready_to_schedule_nodes
        ready_to_schedule_nodes.remove(snode)
        unscheduled_nodes.remove(snode)
        final_order.append(snode)
        for user in tuple_sorted(node_users[snode]):
            if user in indeg:
                indeg[user] -= 1
                if indeg[user] == 0:
                    ready_to_schedule_nodes.add(user)

    def schedule_nodes(snodes):
        """
        Schedules all nodes in `snodes` in an arbitrary topologically valid order.
        """
        all_nodes = set(snodes)
        assert all(node in unscheduled_nodes for node in all_nodes)
        while len(all_nodes) > 0:
            # NOTE: since model graph is always a DAG and does not have circular dependency inside,
            # there should be at least one node that is a "free node" (i.e. indeg == 0),
            # hence infinite loop is not possible. But we check here just to be safe.
            progress = False
            for node in tuple_sorted(all_nodes):
                if node in ready_to_schedule_nodes:
                    schedule_node(node)
                    all_nodes.remove(node)
                    progress = True
            if not progress:
                raise AssertionError(
                    "Unable to find a free node (indeg == 0). This is an impossible state to reach. "
                    "Please report a bug to PyTorch."
                )

    # First, schedule all compute nodes that are required by first comm node,
    # as well as the first comm node itself.
    assert len(comm_nodes) > 0
    schedule_nodes(
        list(comm_ancestors[comm_nodes[0]]) + [comm_nodes[0]],
    )

    rolled_over_compute_cost = 0
    for idx in range(1, len(comm_ancestors)):
        # Step 1: Given that we've currently scheduled comm `idx-1`, we now schedule
        # all compute nodes that are required for comm `idx` but do not depend on comm `idx-1`,
        # to run at the same time with comm `idx-1`.
        needed_by_next_comm_and_ready_compute_nodes = unscheduled_nodes & (
            comm_ancestors[comm_nodes[idx]] - comm_descendants[comm_nodes[idx - 1]]
        )
        assert_no_comm_nodes(needed_by_next_comm_and_ready_compute_nodes)

        total_compute_runtime_cost = rolled_over_compute_cost + sum(
            estimate_op_runtime(node)
            for node in needed_by_next_comm_and_ready_compute_nodes
        )
        prev_comm_runtime_cost = estimate_op_runtime(comm_nodes[idx - 1])
        schedule_nodes(tuple_sorted(needed_by_next_comm_and_ready_compute_nodes))

        # Step 2: If all those compute nodes are sufficient to overlap comm `idx-1`, we're done.
        # Otherwise, we now need to look elsewhere to find compute that overlaps with comm `idx`.
        # We prioritize compute nodes that are needed sooner.
        step1_runtime_cost = total_compute_runtime_cost
        if step1_runtime_cost >= prev_comm_runtime_cost:
            pass
        else:
            # Find all ready to schedule compute nodes that do not depend on comm `idx-1`.
            ready_to_schedule_compute_nodes = tuple_sorted(
                ready_to_schedule_nodes - comm_descendants[comm_nodes[idx - 1]]
            )
            assert_no_comm_nodes(ready_to_schedule_compute_nodes)

            def earliest_comm_descendant(node):
                for idx in range(len(comm_nodes)):
                    if node in comm_ancestors[comm_nodes[idx]]:
                        return idx
                return len(comm_nodes)

            # Prioritize compute nodes that are needed sooner.
            ready_to_schedule_compute_nodes = sorted(
                ready_to_schedule_compute_nodes, key=earliest_comm_descendant
            )

            for snode in ready_to_schedule_compute_nodes:
                if total_compute_runtime_cost >= prev_comm_runtime_cost:
                    # If accumulated compute runtime cost is greater than comm `idx-1` runtime cost,
                    # it means we have maximized overlap for comm `idx-1`, and hence we stop looking
                    # for more compute to schedule.
                    break
                compute_runtime_cost = estimate_op_runtime(snode)
                # If we're not able to leverage more than half of this
                # node's compute to overlap, we skip it.
                # TODO: Smarter heuristics here
                if (
                    prev_comm_runtime_cost - total_compute_runtime_cost
                ) <= compute_runtime_cost / 2:
                    continue
                schedule_node(snode)
                total_compute_runtime_cost += compute_runtime_cost
        rollable_compute_cost = total_compute_runtime_cost - step1_runtime_cost

        # Step 3: We schedule the compute nodes dependent on comm `idx-1` and required for comm `idx`.
        needed_by_next_comm_nodes = unscheduled_nodes & comm_ancestors[comm_nodes[idx]]
        schedule_nodes(list(needed_by_next_comm_nodes))

        # Step 4: We schedule comm `idx`.
        schedule_nodes([comm_nodes[idx]])

        is_prev_comm_blocking_next_comm = len(needed_by_next_comm_nodes) > 0
        # The idea here is that if there are no compute nodes from Step 3
        # (i.e. if prev comm is not blocking next comm), we can roll over the compute nodes
        # in Step 2 to overlap with the next comm, since they're not required to finish
        # before the next comm starts.
        if is_prev_comm_blocking_next_comm:
            rolled_over_compute_cost = 0
        else:
            rolled_over_compute_cost = rollable_compute_cost  # type: ignore[assignment]

    schedule_nodes(unscheduled_nodes)
    return final_order


def node_summary(snode):
    detail = ""
    if isinstance(snode.node, ir.ExternKernelOut):
        detail = f" ({snode.node.python_kernel_name})"
    out_tensor_info = ""
    if (
        hasattr(snode.node, "layout")
        and hasattr(snode.node.layout, "size")
        and hasattr(snode.node.layout, "stride")
    ):
        out_tensor_info = (
            f" (size={snode.node.layout.size}, stride={snode.node.layout.stride})"
        )
    node_name = ""
    if hasattr(snode.node, "name"):
        node_name = snode.node.name
    return f"{snode.node.__class__.__name__}{detail}{out_tensor_info} ({node_name})"


def visualize_overlap(order):
    total_est_runtime: float = 0.0
    cur_comm_node = None
    for snode in order:
        if cur_comm_node is None:
            if is_collective(snode.node):
                total_est_runtime += estimate_op_runtime(snode)
                cur_comm_node = snode.node
            elif is_wait(snode.node):
                raise AssertionError(
                    "Wait is not expected when there is no collective running"
                )
            else:  # exposed compute op
                total_est_runtime += estimate_op_runtime(snode)
            overlap_log.debug(f"{node_summary(snode)}")  # noqa: G004
        else:  # cur_comm_node is not None
            if is_collective(snode.node):
                raise AssertionError(
                    "Found two collectives running at the same time. "
                    "`visualize_overlap` needs to be updated to handle this case"
                )
            elif is_wait(snode.node):  # end of this comm op
                overlap_log.debug(f"{node_summary(snode)}")  # noqa: G004
                cur_comm_node = None
            else:  # overlapped compute op
                overlap_log.debug(f"| {node_summary(snode)}")  # noqa: G004
    overlap_log.debug(
        f"Est. runtime (ms): {total_est_runtime / 1000 / 1000}"  # noqa: G004
    )


def reorder_compute_and_comm_for_overlap(
    snodes: List[BaseSchedulerNode],
) -> List[BaseSchedulerNode]:
    order = snodes
    inverse_users, node_users = compute_node_users(snodes)

    for p in config.reorder_for_compute_comm_overlap_passes:
        if isinstance(p, str) and p in globals():
            p = globals()[p]  # it is a builtin pass
        if torch.distributed.get_rank() == 0:
            overlap_log.debug(
                f"==== Visualize overlap before reordering pass {p} ===="  # noqa: G004
            )
            try:
                visualize_overlap(order)
            except Exception as e:
                overlap_log.debug(str(e))
        order = p(order, node_users, inverse_users)  # type: ignore[operator]
        if torch.distributed.get_rank() == 0:
            overlap_log.debug(
                f"==== Visualize overlap after reordering pass {p} ===="  # noqa: G004
            )
            try:
                visualize_overlap(order)
            except Exception as e:
                overlap_log.debug(str(e))
    return order
