import copy
import logging
from dataclasses import dataclass
from typing import Dict, Iterable, List, Sequence, Tuple

import torch
from torch.fx import GraphModule, Node
from torch.ao.ns.fx.utils import compute_sqnr
from torch.nn import functional as F


__all__ = ["generate_numeric_debug_handle", "NUMERIC_DEBUG_HANDLE_KEY", "prepare_for_propagation_comparison", "extract_results_from_loggers"]

NUMERIC_DEBUG_HANDLE_KEY = "_numeric_debug_handle"


def generate_numeric_debug_handle(graph_module: GraphModule) -> None:
    unique_id = 0
    for node in graph_module.graph.nodes:
        if node.op != "placeholder" and NUMERIC_DEBUG_HANDLE_KEY not in node.meta:
            node.meta[NUMERIC_DEBUG_HANDLE_KEY] = unique_id
            unique_id += 1


class OutputLogger(torch.nn.Module):
    """
    Base class for capturing output values.
    """

    # Mark as impure so that calls to it will not be removed during DCE.
    _is_impure = True

    def __init__(
        self,
        node_name: str,
        nn_module_stack: object,
        debug_handle: str,
    ) -> None:
        super().__init__()
        self.node_name = node_name
        self.nn_module_stack = nn_module_stack
        self.debug_handle = debug_handle
        self.stats: List[torch.Tensor] = []

    def forward(self, x: object) -> object:
        if isinstance(x, torch.Tensor):
            self.stats.append(x.detach())
        return x

    def __repr__(self) -> str:
        clean_dict = {
            k: v
            for k, v in self.__dict__.items()
            # skip torch.nn.Module keys
            if (k != "training") and not k.startswith("_")
        }
        return f"OutputLogger({clean_dict})"


def _insert_logger(model: GraphModule, node: Node, debug_handle: str) -> None:
    """For a given node, adds an OutputLogger that observes the output of that node,
    and all its users use the OutputLogger output instead.
    The OutputLogger will contain the debug_handle which can be used to compare
    graphs after transforms"""

    # to avoid circular dep
    from torch.ao.quantization.fx.utils import get_new_attr_name_with_prefix

    # add a logger after the node
    with model.graph.inserting_after(node):
        get_new_attr_name = get_new_attr_name_with_prefix(f"{node.name}_logger")
        logger_name = get_new_attr_name(model)
        setattr(
            model,
            logger_name,
            OutputLogger(node.name, node.meta.get("nn_module_stack"), debug_handle),
        )
        logger_node = model.graph.call_module(logger_name, (node,), {})

    orig_users = list(node.users.keys())
    for user_node in orig_users:
        if user_node is logger_node:
            continue
        user_node.replace_input_with(node, logger_node)


def prepare_for_propagation_comparison(model: GraphModule) -> GraphModule:
    # don't change the original model
    model = copy.deepcopy(model)
    for n in model.graph.nodes:
        if NUMERIC_DEBUG_HANDLE_KEY not in n.meta:
            continue
        numeric_debug_handle = n.meta[NUMERIC_DEBUG_HANDLE_KEY]
        _insert_logger(model, n, numeric_debug_handle)

    model.recompile()
    return model

@dataclass(frozen=True)
class QuantizationComparisonResult:
    actual: torch.Tensor
    ref: torch.Tensor

    @property
    def mse_loss(self) -> torch.Tensor:
        return F.mse_loss(
            self.actual.to(dtype=torch.float32), self.ref.to(dtype=torch.float32)
        )

    @property
    def sqnr(self) -> torch.Tensor:
        return compute_sqnr(
            self.actual.to(dtype=torch.float32), self.ref.to(dtype=torch.float32)
        )

    def details(self) -> str:
        return f"QuantizationComparisonResult(mse_loss={self.mse_loss}, sqnr={self.sqnr}, actual={self.actual}, ref={self.ref})"

    def __repr__(self) -> str:
        # Don't include the tensors themselves as they are quite large to print
        # out.
        return (
            f"QuantizationComparisonResult(mse_loss={self.mse_loss}, sqnr={self.sqnr})"
        )


@dataclass(frozen=True)
class NodeAccuracySummary:
    handle: str
    actual_node_name: str
    actual_module_stack: str
    ref_node_name: str
    ref_module_stack: str
    results: Sequence[QuantizationComparisonResult]

    def details(self) -> str:
        """Returns a very detailed string for the results including all tensors.
        If you don't want to see this, just use str() or repr() on this object instead
        to get a more concise summary."""
        s = f"{repr(self)}\ndetails:\n"
        for q in self.results:
            s += q.details() + "\n"
        return s


def _module_stack_to_str(module_stack: object) -> str:
    if not isinstance(module_stack, dict):
        return str(module_stack)
    module_values_list = list(module_stack.values())
    if len(module_values_list) > 0:
        owning_module = module_values_list[-1][0]
        return str(owning_module)
    else:
        return str(module_stack)


def extract_results_from_loggers(
    ref_model: GraphModule,
    actual_model: GraphModule,
) -> Dict[str, NodeAccuracySummary]:
    """For each model, extract the comparison of tensors for each debug handle.
    The first model is the reference, the second is after applying a transform.

    Returns:
        A dict is keyed by the node name from the reference (first) model given,
        and the values are a summary of all the tensors that passed through each
    logger and statistics about how different they are from each other."""
    # Results maps debug handle to a tensor list for each model being compared.
    ref_handles: Dict[str, Tuple[str, object, List[torch.Tensor]]] = {}
    actual_handles: Dict[str, Tuple[str, object, List[torch.Tensor]]] = {}
    for _name, module in ref_model.named_children():
        if isinstance(module, OutputLogger):
            ref_handles[module.debug_handle] = (
                module.node_name,
                module.nn_module_stack,
                module.stats,
            )
    for _name, module in actual_model.named_children():
        if isinstance(module, OutputLogger):
            actual_handles[module.debug_handle] = (
                module.node_name,
                module.nn_module_stack,
                module.stats,
            )

    comparisons = {}
    for debug_handle, (ref_name, ref_stack, ref_stats) in ref_handles.items():
        if debug_handle not in actual_handles:
            logging.debug(
                "Cannot compare for handle %s because it wasn't found in the transformed model",
                debug_handle,
            )
            continue
        actual_name, actual_stack, actual_stats = actual_handles[debug_handle]
        comparisons[ref_name] = NodeAccuracySummary(
            handle=debug_handle,
            actual_node_name=actual_name,
            actual_module_stack=_module_stack_to_str(actual_stack),
            ref_node_name=ref_name,
            ref_module_stack=_module_stack_to_str(ref_stack),
            results=[
                QuantizationComparisonResult(actual=a, ref=b)
                for a, b in zip(actual_stats, ref_stats)
            ],
        )

    return comparisons
