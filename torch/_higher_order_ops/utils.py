import functools
from contextlib import contextmanager
from dataclasses import dataclass
from typing import Any, Callable

import torch
import torch.fx.traceback as fx_traceback
import torch.utils._pytree as pytree
from torch._ops import HigherOrderOperator
from torch.fx.experimental.proxy_tensor import make_fx
from torch.multiprocessing.reductions import StorageWeakRef


@dataclass
class UnsupportedAliasMutationException(RuntimeError):
    reason: str


def autograd_not_implemented_inner(
    operator: HigherOrderOperator, delayed_error: bool, *args: Any, **kwargs: Any
) -> Any:
    """If autograd is enabled and any of the arguments require grad this will either
    raise an error or return a DelayedError depending on the value of delayed.

    Args:
        operator: The HigherOrderOperator to call with the *args and **kwargs with
        op_name: The name of the HigherOrderOperator
        delayed_error: If True, return a DelayedError instead of raising an error
        args: The flattened operands to the HigherOrderOperator
        kwargs: The keyword arguments to the HigherOrderOperator

    Raises:
        RuntimeError: If autograd is enabled and any of the arguments to the HigherOrderOperator
    """
    with torch._C._AutoDispatchBelowAutograd():
        result = operator(*args, **kwargs)
        flat_operands = pytree.arg_tree_leaves(*args)
        if torch.is_grad_enabled() and any(
            f.requires_grad for f in flat_operands if isinstance(f, torch.Tensor)
        ):
            if delayed_error:
                err_fn = torch._C._functions.DelayedError(
                    f"Autograd not implemented for {str(operator)}",
                    1,
                )

                def fake_requires_grad(tensor):
                    if torch.is_floating_point(tensor) or torch.is_complex(tensor):
                        tensor = tensor.detach()
                        tensor.requires_grad = True
                    return tensor

                return pytree.tree_map_only(
                    torch.Tensor, lambda x: err_fn(fake_requires_grad(x)), result
                )
            else:
                raise RuntimeError(f"Autograd not implemented for {str(operator)}")
        return result


def autograd_not_implemented(op: HigherOrderOperator, deferred_error: bool) -> Callable:
    def inner(*args, **kwargs):
        return autograd_not_implemented_inner(op, deferred_error, *args, **kwargs)

    return inner


def _maybe_run_with_interpreter(fn):
    maybe_interpreted_fn = fn
    if isinstance(fn, torch.fx.GraphModule) and fx_traceback.has_preserved_node_meta():
        # Running graph with interpreter is needed for propagating the stack_trace
        def graph_with_interpreter(*args):
            with fx_traceback.preserve_node_meta():
                return torch.fx.Interpreter(fn).run(*args)

        maybe_interpreted_fn = graph_with_interpreter
    return maybe_interpreted_fn


def reenter_make_fx(fn):
    from torch.fx.experimental.proxy_tensor import _CURRENT_MAKE_FX_TRACER

    @functools.wraps(fn)
    def wrapped(*args):
        assert (
            _CURRENT_MAKE_FX_TRACER is not None
        ), "Cannot reenter make_fx when we're not under a make_fx tracing session"
        return _CURRENT_MAKE_FX_TRACER.trace_subgraph(
            _maybe_run_with_interpreter(fn), *args
        )

    return wrapped


@contextmanager
def _set_compilation_env():
    _old_is_tracing = torch.fx._symbolic_trace._is_fx_tracing_flag
    try:
        # We need to turn off the is_fx_tracing_flag. Remove this flag check from dyanmo
        # once we are confident fx tracing works with dynamo.
        torch.fx._symbolic_trace._is_fx_tracing_flag = False
        yield
    finally:
        torch.fx._symbolic_trace._is_fx_tracing_flag = _old_is_tracing


def _has_potential_branch_input_mutation(branch, inputs, pre_dispatch=False):
    """
    Dispatch-trace the branch with inputs and check if
    producing graph has mutable op on the input. This is
    bit restrictive as the branch must be traceable.
    """
    try:
        gm = make_fx(branch, pre_dispatch=pre_dispatch)(*inputs)
    except UnsupportedAliasMutationException:
        # this can happen when nested cond_op is
        # functionalized
        return True
    except Exception as e:
        raise e

    def _detect_input_mutation(gm):
        input_nodes = set()
        for node in gm.graph.nodes:
            if node.op == "placeholder":
                input_nodes.add(node)
            if node.op == "call_function":
                target = node.target
                if (
                    isinstance(target, torch._ops.OpOverload)
                    and target._schema.is_mutable
                ):
                    for arg in node.args:
                        if arg in input_nodes:
                            return True

        for _, module in gm.named_children():
            if isinstance(module, torch.fx.GraphModule):
                if _detect_input_mutation(module):
                    return True

        return False

    return _detect_input_mutation(gm)


def _has_potential_branch_input_alias(branch, inputs, pre_dispatch=False):
    """
    Dispatch-trace the branch with inputs and check if
    producing graph has output aliasing the branch input. This is
    bit restrictive as the branch must be traceable.
    """
    try:
        gm = make_fx(branch, pre_dispatch=pre_dispatch)(*inputs)
    except UnsupportedAliasMutationException:
        # this can happen when nested cond_op is
        # functionalized
        return True
    except Exception as e:
        raise e

    def _detect_input_alias(gm):
        input_storages = set()
        for node in gm.graph.nodes:
            # We need to check existence of "val" because we reuse the logic here
            # for map operator, where num_mapped_args is a scalar
            # and doesn't have a "val" meta.
            if node.op == "placeholder" and "val" in node.meta:
                input_storages.add(StorageWeakRef(node.meta["val"]._typed_storage()))
            if node.op == "output":

                def check_alias(out):
                    if out is not None and "val" in out.meta:
                        out_storage = StorageWeakRef(out.meta["val"]._typed_storage())
                        return out_storage in input_storages
                    return False

                if any(pytree.tree_leaves(pytree.tree_map(check_alias, node.args))):
                    return True

        for _, module in gm.named_children():
            if isinstance(module, torch.fx.GraphModule) and _detect_input_alias(module):
                return True

        return False

    return _detect_input_alias(gm)


def unique_graph_id(proxy_mode, prefix):
    """Returns a unique name and id for a graph to be added to a proxy_mode tracer"""
    # There are probably better ways - I know that create_arg has some self incrementing name
    # magic to it, but since we explicitly have to get the name for register_module,
    # I was not sure how to do that. This kinda simulates it.
    next_name = None
    i = 0
    while not next_name:
        candidate = f"{prefix}_{i}"
        if hasattr(proxy_mode.tracer.root, candidate):
            i += 1
        else:
            next_name = candidate
    return i, next_name

def _from_fun(t):
    from torch._subclasses.functional_tensor import (
        FunctionalTensor,
    )
    from torch._functorch.aot_autograd import from_fun

    if isinstance(t, torch.Tensor):
        if t.dtype != torch.bool:
            return torch.empty_strided(
                t.size(),
                t.stride(),
                dtype=t.dtype,
                requires_grad=t.requires_grad,
            )
        else:
            # clone of a functional tensor produces a functional tensor
            # but we want to avoid it so we clone a non-functional version
            maybe_unfunc_t = t
            if isinstance(t, FunctionalTensor):
                torch._sync(t)
                maybe_unfunc_t = from_fun(t)
            elif torch._is_functional_tensor(t):
                # need to handle both types of functionalization here:
                # these are the tensors that came from the user,
                # which could be either FunctionalTensorWrapper or FunctionalTensor
                torch._sync(t)
                maybe_unfunc_t = torch._from_functional_tensor(t)
            return maybe_unfunc_t.clone()
    return t

def clone_outputs_aliasing_inputs(args):
    input_storage = {
        StorageWeakRef(arg._typed_storage())
        for arg in args
        if isinstance(arg, torch.Tensor)
    }

    def maybe_clone(t):
        if (
            isinstance(t, torch.Tensor)
            and StorageWeakRef(t._typed_storage()) in input_storage
        ):
            return t.clone()
        return t
    
    return maybe_clone

def prepare_fw_with_masks(fn):
    def fw_with_masks(*args):
        fw_out = fn(*args)
        return fw_out, [
            True
            if isinstance(ret, torch.Tensor) and ret.requires_grad
            else False
            for ret in fw_out
        ]
    return fw_with_masks

def create_fw_bw_graph(fn, use_output_and_grad_bw, num_mapped_args, *args):
    from torch._functorch.aot_autograd import AOTConfig, create_joint
    dummy_aot_config = AOTConfig(
        fw_compiler=None,  # type: ignore[arg-type]
        bw_compiler=None,  # type: ignore[arg-type]
        partition_fn=None,  # type: ignore[arg-type]
        decompositions={},
        num_params_buffers=0,
        aot_id=0,
        keep_inference_input_mutations=False,
    )
    
    operands = args[:num_mapped_args]
    pos_args = args[num_mapped_args:]
    
    example_flat_out = pytree.tree_map(
        _from_fun, fn(*operands)
    )
    example_grad = [_from_fun(out) for out in example_flat_out]
    num_grads = len(example_grad)
    fw_graph = make_fx(fn)(*operands)

    def joint_fn(*joint_mapped_args):

        # if use_output_and_grad_bw:
        #     mapped_grads = joint_mapped_args[0]
        #     mapped_input = joint_mapped_args[1][:num_mapped_args][-1:]
        #     mapped_pos_args = joint_mapped_args[1][num_mapped_args:]
        # else:
        #     mapped_grads = joint_mapped_args[:num_grads]
        #     mapped_input = joint_mapped_args[num_grads:num_grads+num_mapped_args]
        #     mapped_pos_args = joint_mapped_args[num_grads+num_mapped_args:]
            
        mapped_grads = joint_mapped_args[0]
        mapped_input = joint_mapped_args[1][:num_mapped_args][-1:]
        mapped_pos_args = joint_mapped_args[1][num_mapped_args:]
        
        bw_path = False
        
        if len(mapped_pos_args) > 0 and mapped_pos_args[0]:
            bw_path = True

        joint = create_joint(prepare_fw_with_masks(fn), aot_config=dummy_aot_config)
        if bw_path:
            grads = list(mapped_grads)
        else:
            _, grads = joint(
                list(mapped_input),
                [
                    grad
                    for grad in mapped_grads
                    if grad is not None and grad.requires_grad
                ],
            )

        # In order to keep map functional for backward graph,
        # we clone outputs that are aliasing inputs           
        maybe_clone = clone_outputs_aliasing_inputs(joint_mapped_args)

        return pytree.tree_map(maybe_clone, grads)
    
    # if use_output_and_grad_bw:
    #     example_xs_out = list(operands) + list(example_flat_out)
    #     num_mapped_args = len(example_xs_out)
    #     example_xs_out = example_xs_out + list(pos_args)
    #     joint_operands_grads = (list(example_grad), list(example_xs_out))
    # else:
    #     example_xs_out = list(operands) + list(pos_args)
    #     joint_operands_grads = list(example_grad) + list(example_xs_out)
        
    example_xs_out = list(operands) + list(example_flat_out)
    num_mapped_args = len(example_xs_out)
    example_xs_out = example_xs_out + list(pos_args)
    joint_operands_grads = (list(example_grad), list(example_xs_out))
        
    joint_graph = make_fx(joint_fn)(*joint_operands_grads)
    return fw_graph, joint_graph

def _unstack_pytree(xs):
    flat_xs, inspec = pytree.tree_flatten(xs)
    if not all(isinstance(xs, torch.Tensor) for xs in flat_xs):
        raise RuntimeError(f"Leaves of xs must be Tensor {flat_xs}")

    if not all(xs.shape[0] == flat_xs[0].shape[0] for xs in flat_xs):
        raise RuntimeError(
            f"Leaves of xs must have same leading dimension size {[xs.shape for xs in flat_xs]}"
        )

    a = zip(*flat_xs)

    pytrees = []
    for tuple in a:
        pytrees.append(pytree.tree_unflatten(tuple, inspec))
    return pytrees


def _stack_pytree(pytrees):
    flat_out = []
    out_spec = None
    for pt in pytrees:
        flat_pt, out_spec = pytree.tree_flatten(pt)
        flat_out.append(flat_pt)
    assert out_spec is not None
    b = zip(*flat_out)
    stacked_out = []
    for leaves in b:
        if all(isinstance(leaf, torch.Tensor) for leaf in leaves):
            stacked_out.append(torch.stack(leaves))
        elif all(leaf is None for leaf in leaves):
            # Backward graph can return None output when forward inputs doesn't require grad.
            # When we eagerly execute backward graph, we need to call _stack_pytree on its output,
            # therefore we need to deal with None output.
            stacked_out.append(None)  # type: ignore[arg-type]
        else:
            raise RuntimeError(f"Cannot stack {leaves}.")
    return pytree.tree_unflatten(stacked_out, out_spec)