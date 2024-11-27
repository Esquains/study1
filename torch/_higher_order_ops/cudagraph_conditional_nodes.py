# mypy: allow-untyped-defs
from contextlib import contextmanager
from typing import Any, Generator, Optional
from typing_extensions import Self

import torch
from torch.utils._python_dispatch import TorchDispatchMode


# I need to somehow use this in the backward pass as well. How? Could
# be quite easy actually...
class ControlFlowOpWarmupDispatchMode(TorchDispatchMode):
    def __init__(
        self,
    ) -> None:
        self.stream = torch.cuda.graphs.create_external_stream()
        self.throw_away_graph: Optional[torch.cuda.CUDAGraph] = None
        self.graph_ctx: Optional[torch.cuda.graph] = None
        self.original_capture_mode = None
        self.autograd_multithreading_disabled = None

    def __enter__(self) -> Self:
        self.throw_away_graph = torch.cuda.CUDAGraph()
        # relaxed stream capture can still fail if a synchronizing API
        # is called. But then this workload could not be captured in a
        # cuda graph anyway, so such a failure is fine.
        self.graph_ctx = torch.cuda.graph(
            self.throw_away_graph,
            stream=self.stream,
            capture_error_mode="relaxed",
            collect_garbage=False,
        )
        cudart = torch.cuda.cudart()
        self.original_capture_mode = cudart.cudaThreadExchangeStreamCaptureMode(
            cudart.cudaStreamCaptureMode.Relaxed
        )
        self.graph_ctx.__enter__()
        self.autograd_multithreading_disabled = (
            torch.autograd.grad_mode.set_multithreading_enabled(False)
        )
        self.autograd_multithreading_disabled.__enter__()
        super().__enter__()
        return self

    def __exit__(
        self,
        exc_type,
        exc_val,
        exc_tb,
    ) -> None:
        super().__exit__(exc_type, exc_val, exc_tb)
        self.autograd_multithreading_disabled.__exit__(exc_type, exc_val, exc_tb)
        assert self.graph_ctx is not None
        self.graph_ctx.__exit__(exc_type, exc_val, exc_tb)
        # The destructor of self.throw_away_graph calls
        # cudaGraphExecDestroy(), which is an unsafe call for any
        # other streams that are currently capturing to a graph. To
        # prevent invalidating other capturing streams, this thread
        # must remain in relaxed stream capture mode when the
        # destructor runs. Therefore, we manually delete
        # self.throw_away_graph (and self.graph_ctx, which has a
        # strong reference to it) now rather than letting them be
        # automatically destroyed when this
        # ControlFlowOpWarmupDispatchMode instance is deleted.
        del self.graph_ctx
        del self.throw_away_graph
        cudart = torch.cuda.cudart()
        previous_capture_mode = cudart.cudaThreadExchangeStreamCaptureMode(
            self.original_capture_mode
        )
        assert previous_capture_mode == cudart.cudaStreamCaptureMode.Relaxed

    def __torch_dispatch__(
        self,
        func,
        types,
        args=(),
        kwargs=None,
    ):
        kwargs = {} if kwargs is None else kwargs
        return func(*args, **kwargs)


def _is_boolean_scalar_cuda_tensor(pred: Any) -> bool:
    return (
        isinstance(pred, torch.Tensor)
        and pred.size() == torch.Size([])
        and pred.dtype == torch.bool
        and pred.is_cuda
    )


@contextmanager
def _if_body(pred: torch.Tensor) -> Generator[None, None, None]:
    current_cuda_graph = torch.cuda.CUDAGraph.get_currently_capturing_graph()
    current_cuda_graph.begin_capture_to_if_node(pred)
    try:
        yield
    finally:
        current_cuda_graph.end_capture_to_conditional_node()


def if_else_node(pred: torch.Tensor, true_fn, false_fn, operands):
    if not pred.is_cuda:
        raise ValueError(
            "Conditions must be on a cuda device to use conditional node in cuda graphs"
        )
    # if-else is not supported yet in CUDA 12.4. Therefore, we use two if conditions, where one evaluates !pred
    outs = []

    for lazy_pred, fn in [
        (lambda: pred, true_fn),
        (lambda: torch.logical_not(pred), false_fn),
    ]:
        with _if_body(lazy_pred()):
            outs.append(fn(*operands))
            # Copy these two outputs into a new output buffer. Well,
            # actually, what we would like is to be able to merge these two
            # tensors into the same tensor... Is there an obvious way to do
            # that?
            if len(outs) == 2:
                for if_out, else_out in zip(outs[0], outs[1]):
                    if_out.copy_(else_out)
    assert len(outs) == 2
    return outs[0]


@contextmanager
def _while_loop_body(pred: torch.Tensor) -> Generator[int, None, None]:
    current_cuda_graph = torch.cuda.CUDAGraph.get_currently_capturing_graph()
    conditional_handle = current_cuda_graph.begin_capture_to_while_loop_node(pred)
    try:
        yield conditional_handle
    finally:
        current_cuda_graph.end_capture_to_conditional_node()


def while_loop_node(cond_fn, body_fn, carried_inputs, additional_inputs):
    if not isinstance(carried_inputs, tuple):
        raise RuntimeError(
            f"carried_inputs must be a tuple but got {type(carried_inputs)}"
        )

    carried_vals = carried_inputs
    pred = cond_fn(*carried_vals, *additional_inputs)
    if not _is_boolean_scalar_cuda_tensor(pred):
        raise RuntimeError(
            f"cond_fn must return a boolean scalar cuda tensor but got {pred}"
        )

    with _while_loop_body(pred) as conditional_handle:
        out = body_fn(*carried_vals, *additional_inputs)
        assert isinstance(
            out, tuple
        ), f"body_fn should return a tuple but got {type(out)}"
        assert len(out) == len(
            carried_inputs
        ), "body_fn should return the same number of elements as carried_inputs"

        for c, o in zip(carried_vals, out):
            c.copy_(o)

        # call the cond_fn again to update the pred
        pred = cond_fn(*carried_vals, *additional_inputs)
        if not _is_boolean_scalar_cuda_tensor(pred):
            raise RuntimeError(
                f"cond_fn must return a boolean scalar tensor but got {pred}"
            )
        torch.cuda.CUDAGraph.set_conditional_handle(conditional_handle, pred)

    return carried_vals
