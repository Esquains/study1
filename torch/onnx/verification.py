"""Functions to verify exported ONNX model is functionally equivalent to original PyTorch model.

ONNX Runtime is required, and is used as the ONNX backend for export verification.
"""


import contextlib
import copy
import difflib
import io
import os
import tempfile
import warnings
from typing import Any, Mapping, Optional, Sequence, Set, Tuple, Type, Union

import numpy as np

import torch
import torch._C._onnx as _C_onnx
from torch import _C, Tensor
from torch.onnx import _constants, symbolic_helper, utils
from torch.onnx.utils import unpack_quantized_tensor

_ORT_PROVIDERS = ("CPUExecutionProvider",)


def _flatten_tuples(elem):
    flattened = []
    for t in elem:
        if isinstance(t, tuple):
            flattened.extend(_flatten_tuples(t))
        else:
            flattened.append(t)
    return flattened


def _to_numpy(elem):
    if isinstance(elem, Tensor):
        if elem.requires_grad:
            return elem.detach().cpu().numpy()
        else:
            return elem.cpu().numpy()
    elif isinstance(elem, (list, tuple)):
        return [_to_numpy(inp) for inp in elem]
    elif isinstance(elem, (bool, int, float)):
        return np.array(elem)
    elif isinstance(elem, dict):
        flattened = []
        for k in elem:
            flattened += [_to_numpy(k)] + [_to_numpy(elem[k])]
        return flattened
    return elem


def _inline_flatten_list(inputs, res_list):
    for i in inputs:
        res_list.append(i) if not isinstance(
            i, (list, tuple)
        ) else _inline_flatten_list(i, res_list)
    return res_list


def _unpack_to_numpy(values):
    value_unpacked = []
    for value in values:
        value_unpacked.extend(unpack_quantized_tensor(value))
    return [_to_numpy(v) for v in value_unpacked]


def _run_ort(ort_session, inputs):
    kw_inputs = {}
    if inputs and isinstance(inputs[-1], dict):
        kw_inputs = inputs[-1]
        inputs = inputs[:-1]
    inputs = _unpack_to_numpy(_flatten_tuples(inputs))
    ort_inputs = {}
    for input_name, input in kw_inputs.items():
        ort_inputs[input_name] = _to_numpy(input)
    inputs = _to_numpy(inputs)
    ort_session_inputs = ort_session.get_inputs()
    for i, input in enumerate(inputs):
        if i == len(ort_session_inputs) or ort_session_inputs[i].name in ort_inputs:
            raise ValueError(
                f"got too many positional inputs. inputs: {inputs}. kw_inputs: {kw_inputs}"
            )
        ort_inputs[ort_session_inputs[i].name] = input
    ort_outs = ort_session.run(None, ort_inputs)
    return _inline_flatten_list(ort_outs, [])


def _ort_session(
    model: Union[str, io.BytesIO], ort_providers: Sequence[str] = _ORT_PROVIDERS
):
    try:
        import onnxruntime  # type: ignore[import]
    except ImportError:
        raise ImportError("onnxruntime is required for export verification.")

    if ort_providers is None:
        ort_providers = _ORT_PROVIDERS

    session_options = onnxruntime.SessionOptions()
    # suppress ort warnings.
    # 0:Verbose, 1:Info, 2:Warning. 3:Error, 4:Fatal. Default is 2.
    session_options.log_severity_level = 3
    ort_session = onnxruntime.InferenceSession(
        model if isinstance(model, str) else model.getvalue(),
        session_options,
        providers=ort_providers,
    )
    return ort_session


def _compare_ort_pytorch_outputs(ort_outs, pt_outs, rtol, atol):
    pt_outs, _ = torch.jit._flatten(pt_outs)
    pt_outs = _unpack_to_numpy(pt_outs)

    assert len(pt_outs) == len(ort_outs), "number of outputs differ"

    for ort_out, pt_out in zip(ort_outs, pt_outs):
        np.testing.assert_allclose(ort_out, pt_out, rtol=rtol, atol=atol)


def _prepare_input_for_pytorch(args, kwargs):
    """Prepare input for PyTorch model execution.

    Any future changes/formatting to the input before dispatching to the PyTorch
    model should be made in this function.

    Args:
        args: positional arguments for PyTorch model forward method.
        kwargs: keyword arguments for PyTorch model forward method.

    Returns:
        args: positional arguments for PyTorch model forward method.
        kwargs: keyword arguments for PyTorch model forward method.
    """
    if isinstance(args, (Tensor, dict)):
        args = (args,)
    # In-place operators will update input tensor data as well.
    # Thus inputs are replicated before every forward call.
    args = copy.deepcopy(args)
    if kwargs:
        kwargs = copy.deepcopy(kwargs)
    else:
        kwargs = {}
    return args, kwargs


def _prepare_input_for_export(args, kwargs):
    """Prepare input for ONNX model export.

    Any future changes/formatting to the input before dispatching to the
    :func:`torch.onnx.export` api should be made in this function.

    Args:
        args: positional arguments for PyTorch model forward method.
        kwargs: keyword arguments for PyTorch model forward method.

    Returns:
        onnx_inputs: positional arguments for ONNX model export, as `args` in
            :func:`torch.onnx.export`.
    """
    args, kwargs = _prepare_input_for_pytorch(args, kwargs)
    if not kwargs and isinstance(args[-1], dict):
        onnx_inputs = args + ({},)
    elif kwargs:
        onnx_inputs = args + (kwargs,)
    else:
        onnx_inputs = args
    return onnx_inputs


def _prepare_input_for_ort(args, kwargs, remained_onnx_input_idx, flatten):
    """Prepare input for ONNX model execution in ONNX Runtime.

    Any future changes/formatting to the input before dispatching to the ONNX Runtime
    InferenceSession run should be made in this function.

    Args:
        args: positional arguments for PyTorch model forward method.
        kwargs: keyword arguments for PyTorch model forward method.

    Returns:
        onnx_inputs: positional arguments for ONNX model execution in ONNX Runtime.
    """
    onnx_inputs = _prepare_input_for_export(args, kwargs)
    if flatten:
        onnx_inputs, _ = torch.jit._flatten(onnx_inputs)
    elif onnx_inputs and onnx_inputs[-1] == {}:
        # Handle empty kwargs (normally removed by flatten).
        onnx_inputs = onnx_inputs[:-1]
    if remained_onnx_input_idx is not None:
        return [onnx_inputs[i] for i in remained_onnx_input_idx]
    else:
        return onnx_inputs


def _try_clone_model(model):
    """Used for preserving original model in case forward mutates model states."""
    try:
        return copy.deepcopy(model)
    except Exception:
        warnings.warn(
            "Failed to clone model. Model state might be mutated during verification."
        )
        return model


def _compare_ort_pytorch_model(
    model,
    ort_session,
    input_args,
    input_kwargs,
    additional_test_inputs,
    remained_onnx_input_idx,
    flatten,
    rtol,
    atol,
):
    """Compare outputs from ONNX model runs with outputs from PyTorch model runs.

    ONNX Runtime is used for model execution backend for ONNX model.

    Raises:
        AssertionError: if outputs from ONNX model and PyTorch model are not
            equal up to specified precision.
    """

    def compare_ort_pytorch_model_with_input(input_args, input_kwargs):
        pt_args, pt_kwargs = _prepare_input_for_pytorch(input_args, input_kwargs)
        # TODO: remove this and treat mutating model separately. See #77679
        model_copy = _try_clone_model(model)
        pt_outs = model_copy(*pt_args, **pt_kwargs)

        ort_inputs = _prepare_input_for_ort(
            input_args, input_kwargs, remained_onnx_input_idx, flatten
        )
        ort_outs = _run_ort(ort_session, ort_inputs)

        _compare_ort_pytorch_outputs(ort_outs, pt_outs, rtol, atol)

    compare_ort_pytorch_model_with_input(input_args, input_kwargs)

    if additional_test_inputs:
        for test_input_args in additional_test_inputs:
            compare_ort_pytorch_model_with_input(test_input_args, {})


class ExportOptions:
    """Arguments used by :func:`torch.onnx.export`.

    TODO: Adopt this in `torch.onnx.export` api to replace keyword arguments.
    """

    export_params: bool = True
    verbose: bool = False
    training: _C_onnx.TrainingMode = _C_onnx.TrainingMode.EVAL
    input_names: Optional[Sequence[str]] = None
    output_names: Optional[Sequence[str]] = None
    operator_export_type: _C_onnx.OperatorExportTypes = _C_onnx.OperatorExportTypes.ONNX
    opset_version: Optional[int] = None
    do_constant_folding: bool = True
    dynamic_axes: Optional[Mapping[str, Union[Mapping[int, str], Sequence[int]]]] = None
    keep_initializers_as_inputs: Optional[bool] = None
    custom_opsets: Optional[Mapping[str, int]] = None
    export_modules_as_functions: Union[bool, Set[Type[torch.nn.Module]]] = False

    def __init__(self, **kwargs):
        self.__dict__.update(kwargs)


class _GraphDiff:
    """A class to represent the difference between two graphs."""

    def __init__(self, graph_a: _C.Graph, graph_b: _C.Graph):
        """Construct a _GraphDiff object.

        Args:
            graph_a (_C.Graph): First graph to compare.
            graph_b (_C.Graph): Second graph to compare.
        """
        self.graph_a = graph_a
        self.graph_b = graph_b

    def __str__(self):
        """See function :func:`_graph_diff`."""
        return self.diff_report()

    def _indent(self, s):
        return "\n".join(["\t" + line for line in s.splitlines()])

    def diff_report(self):
        """Return a string representation of the graph difference.

        The report shows the first pair of nodes that diverges. It also shows the source
        location of the pair of nodes.

        Returns:
            graph_diff_report (str): A string representation of the graph difference.
        """
        graph_a = self.graph_a
        graph_b = self.graph_b

        graph_a_str = str(graph_a)
        graph_b_str = str(graph_b)

        if graph_a_str == graph_b_str:
            return ""

        graph_diff = difflib.ndiff(
            graph_a_str.splitlines(True), graph_b_str.splitlines(True)
        )
        graph_diff_report = "Graph diff:\n" + self._indent("".join(graph_diff)) + "\n"

        for n_ref, n_check in zip(graph_a.nodes(), graph_b.nodes()):
            if str(n_ref) != str(n_check):
                graph_diff_report += "First diverging operator:\n"
                node_diff = difflib.ndiff(
                    str(n_ref).splitlines(True), str(n_check).splitlines(True)
                )
                source_printout = f"node diff:\n{self._indent(''.join(node_diff))}\n"

                ref_stack = n_ref.sourceRange()
                if ref_stack:
                    source_printout += (
                        f"Reference source location:\n{self._indent(ref_stack)}\n"
                    )
                check_stack = n_check.sourceRange()
                if check_stack:
                    source_printout += (
                        f"Check source location:\n{self._indent(check_stack)}\n"
                    )

                graph_diff_report += source_printout

                break

        return graph_diff_report


def _check_jit_model_diff(
    model: Union[torch.nn.Module, torch.jit.ScriptModule],
    test_inputs: Sequence[Tuple[Tuple[Any, ...], Mapping[str, Any]]],
    export_options: ExportOptions,
):
    if len(test_inputs) <= 1:
        raise ValueError("Need at least two set of test inputs to compare.")

    training = export_options.training
    verbose = export_options.verbose

    with utils.exporter_context(model, training, verbose):
        ref_jit_graph = None
        for args, kwargs in test_inputs:
            export_inputs = _prepare_input_for_export(args, kwargs)
            jit_graph, _, _, _ = utils._create_jit_graph(model, export_inputs)

            if ref_jit_graph is None:
                ref_jit_graph = jit_graph
                continue

            graph_diff_report = _GraphDiff(ref_jit_graph, jit_graph).diff_report()
            if graph_diff_report:
                return graph_diff_report

    return ""


def _check_onnx_model_diff(
    model: Union[torch.nn.Module, torch.jit.ScriptModule],
    test_inputs: Sequence[Tuple[Tuple[Any, ...], Mapping[str, Any]]],
    export_options: ExportOptions,
):
    if len(test_inputs) <= 1:
        raise ValueError("Need at least two set of test inputs to compare.")

    # TODO: refactor utils.py to remove duplicated code of context setup.
    opset_version = export_options.opset_version
    operator_export_type = export_options.operator_export_type
    export_modules_as_functions = export_options.export_modules_as_functions
    training = export_options.training
    verbose = export_options.verbose
    dynamic_axes = export_options.dynamic_axes
    input_names = export_options.input_names
    output_names = export_options.output_names

    if opset_version is None:
        opset_version = _constants.onnx_default_opset

    export_modules_as_functions = utils._setup_trace_module_map(
        model, export_modules_as_functions
    )

    if not operator_export_type:
        if _C_onnx._CAFFE2_ATEN_FALLBACK:
            operator_export_type = _C_onnx.OperatorExportTypes.ONNX_ATEN_FALLBACK
        else:
            operator_export_type = _C_onnx.OperatorExportTypes.ONNX

    symbolic_helper._set_opset_version(opset_version)
    symbolic_helper._set_operator_export_type(operator_export_type)

    with utils.exporter_context(model, training, verbose):
        val_do_constant_folding = utils._decide_constant_folding(
            export_options.do_constant_folding, operator_export_type, training
        )

        if dynamic_axes is None:
            dynamic_axes = {}
        utils._validate_dynamic_axes(dynamic_axes, model, input_names, output_names)

        ref_onnx_graph = None
        for arg, kwargs in test_inputs:
            export_inputs = _prepare_input_for_export(arg, kwargs)
            export_inputs = utils._decide_input_format(model, export_inputs)
            onnx_graph, _, _ = utils._model_to_graph(
                model,
                export_inputs,
                verbose,
                input_names,
                output_names,
                operator_export_type,
                val_do_constant_folding,
                training=training,
                dynamic_axes=dynamic_axes,
            )

            if ref_onnx_graph is None:
                ref_onnx_graph = onnx_graph
                continue

            graph_diff_report = _GraphDiff(ref_onnx_graph, onnx_graph).diff_report()
            if graph_diff_report:
                return graph_diff_report

    return ""


def check_export_model_diff(
    model: Union[torch.nn.Module, torch.jit.ScriptModule],
    test_inputs: Sequence[Tuple[Tuple[Any, ...], Mapping[str, Any]]],
    export_options: Optional[ExportOptions] = None,
):
    """Verify exported model discrepancy between different sets of inputs.

    A graph is exported for each set of inputs. The exported graphs are then compared
    to each other, and discrepancies are reported. This function first checks the jit
    graph, and then the onnx graph.

    Unless otherwise specified, the jit/ONNX graph is expected to be the same, regardless
    of the inputs it used for exporting. A discrepancy would imply the graph exported is
    not accurate when running with other set of inputs, which will typically results in
    runtime error or output mismatches.

    Args:
        model (torch.nn.Module or torch.jit.ScriptModule): The model to be exported.
        test_inputs (Sequence[Tuple[Tuple[Any, ...], Mapping[str, Any]]]): A sequence of
            inputs to be used to export the model.
        export_options (ExportOptions, optional): An ExportOptions object that
            controls the export behavior.

    Returns:
        str: A string containing the diff of the exported models.
    """
    export_options = ExportOptions() if export_options is None else export_options

    # TODO: refactor utils.py to remove duplicated code of context setup.
    opset_version = export_options.opset_version
    if opset_version is None:
        opset_version = _constants.onnx_default_opset
    symbolic_helper._set_opset_version(opset_version)
    symbolic_helper._set_onnx_shape_inference(True)

    jit_diff_report = _check_jit_model_diff(model, test_inputs, export_options)
    if jit_diff_report:
        return jit_diff_report

    return _check_onnx_model_diff(model, test_inputs, export_options)


def verify(
    model: Union[torch.nn.Module, torch.jit.ScriptModule],
    input_args: Tuple[Any, ...],
    input_kwargs: Optional[Mapping[str, Any]] = None,
    do_constant_folding: bool = True,
    dynamic_axes: Optional[
        Mapping[str, Union[Mapping[int, str], Mapping[str, Sequence[int]]]]
    ] = None,
    input_names: Optional[Sequence[str]] = None,
    output_names: Optional[Sequence[str]] = None,
    training: Optional[bool] = None,
    opset_version: Optional[int] = None,
    keep_initializers_as_inputs: bool = True,
    verbose: bool = False,
    fixed_batch_size: bool = False,
    use_external_data: bool = False,
    additional_test_inputs: Optional[Sequence[Tuple[Any, ...]]] = None,
    remained_onnx_input_idx: Optional[Sequence[int]] = None,
    flatten: bool = True,
    ort_providers: Sequence[str] = _ORT_PROVIDERS,
    rtol: float = 0.001,
    atol: float = 1e-7,
    **kwargs,
):
    """Verify model export to ONNX with ONNX Runtime.

    Args:
        model (torch.nn.Module or torch.jit.ScriptModule): See :func:`torch.onnx.export`.
        input_args (tuple): See :func:`torch.onnx.export`.
        input_kwargs (dict): See :func:`torch.onnx.export`.
        do_constant_folding (bool, optional): See :func:`torch.onnx.export`.
        dynamic_axes (dict, optional): See :func:`torch.onnx.export`.
        input_names (list, optional): See :func:`torch.onnx.export`.
        output_names (list, optional): See :func:`torch.onnx.export`.
        training (bool, optional): See :func:`torch.onnx.export`.
        opset_version (int, optional): See :func:`torch.onnx.export`.
        keep_initializers_as_inputs (bool, optional): See :func:`torch.onnx.export`.
        verbose (bool, optional): See :func:`torch.onnx.export`.
        fixed_batch_size (bool, optional): Legacy argument, used only by rnn test cases.
        use_external_data (bool, optional): Explicitly specify whether to export the
            model with external data.
        additional_test_inputs (list, optional): List of tuples. Each tuple is a set of
            input arguments to test. Currently only *args are supported.
        remained_onnx_input_idx (list, optional): If set, only the specified inputs will
            be passed to ONNX model. This is used when there are unused inputs in original
            PyTorch model. ONNX model will remove unused inputs automatically.
        flatten (bool, optional): Default True. If True, unpack nested list/tuple/dict
            inputs into a flattened list of Tensors for ONNX. Set this to False if nested
            structures are to be preserved for ONNX, which is usually the case with
            exporting ScriptModules.
        ort_providers (sequence, optional): ONNX Runtime providers to use.
        rtol (float, optional): relative tolerance in comparison between ONNX and PyTorch outputs.
        atol (float, optional): absolute tolerance in comparison between ONNX and PyTorch outputs.

    Raises:
        AssertionError: if outputs from ONNX model and PyTorch model are not
            equal up to specified precision.
    """
    if training is not None and training == torch.onnx.TrainingMode.TRAINING:
        model.train()
    elif training is None or training == torch.onnx.TrainingMode.EVAL:
        model.eval()
    with torch.no_grad(), contextlib.ExitStack() as stack:
        model_f: Union[str, io.BytesIO] = io.BytesIO()
        if use_external_data:
            tmpdirname = stack.enter_context(tempfile.TemporaryDirectory())
            model_f = os.path.join(tmpdirname, "model.onnx")

        inputs_for_export = _prepare_input_for_export(input_args, input_kwargs)

        # TODO: remove this and treat mutating model separately. See #77679
        model_copy = _try_clone_model(model)
        torch.onnx._export(
            model,
            inputs_for_export,
            model_f,
            opset_version=opset_version,
            do_constant_folding=do_constant_folding,
            keep_initializers_as_inputs=keep_initializers_as_inputs,
            dynamic_axes=dynamic_axes,
            input_names=input_names,
            output_names=output_names,
            fixed_batch_size=fixed_batch_size,
            training=training,
            verbose=verbose,
        )

        ort_session = _ort_session(model_f, ort_providers)

        _compare_ort_pytorch_model(
            model_copy,
            ort_session,
            input_args,
            input_kwargs,
            additional_test_inputs,
            remained_onnx_input_idx,
            flatten,
            rtol,
            atol,
        )
