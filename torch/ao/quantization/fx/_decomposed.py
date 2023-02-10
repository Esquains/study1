import torch
from torch.library import Library, impl
from torch.ao.quantization.utils import determine_qparams, validate_qmin_qmax
from typing import Tuple
from torch._meta_registrations import calc_conv_nd_return_shape

# Note: decomposed means decomposed quantized tensor, using decomposed so that the
# name is not too long
quantized_decomposed_lib = Library("quantized_decomposed", "DEF")

_DTYPE_TO_QVALUE_BOUNDS = {
    torch.uint8: (0, 255),
    torch.int8: (-128, 127),
    torch.int32: (-(2**31), 2**31 - 1)
}

# Helper to check the passed in quant min and max are valid for the dtype
def _quant_min_max_bounds_check(quant_min, quant_max, dtype):
    if dtype not in _DTYPE_TO_QVALUE_BOUNDS:
        raise ValueError(f"Unsupported dtype: {dtype}")
    quant_min_lower_bound, quant_max_upper_bound = _DTYPE_TO_QVALUE_BOUNDS[dtype]

    assert quant_min >= quant_min_lower_bound, \
        "quant_min out of bound for dtype, " \
        f"quant_min_lower_bound: {quant_min_lower_bound} quant_min: {quant_min}"

    assert quant_max <= quant_max_upper_bound, \
        "quant_max out of bound for dtype, " \
        f"quant_max_upper_bound: {quant_max_upper_bound} quant_max: {quant_max}"

quantized_decomposed_lib.define(
    "quantize_per_tensor(Tensor input, float scale, int zero_point, "
    "int quant_min, int quant_max, ScalarType dtype) -> Tensor")

@impl(quantized_decomposed_lib, "quantize_per_tensor", "CompositeExplicitAutograd")
def quantize_per_tensor(
        input: torch.Tensor,
        scale: float,
        zero_point: int,
        quant_min: int,
        quant_max: int,
        dtype: torch.dtype
) -> torch.Tensor:
    """ Affine quantization for the Tensor using the same quantization parameters to map
    from floating point to quantized values

    Args:
       input (torch.Tensor): original float32 Tensor
       scale (float): quantization parameter for affine quantization
       zero_point (int): quantization parameter for affine quantization
       quant_min (int): minimum quantized value for output Tensor
       quant_max (int): maximum quantized value for output Tensor
       dtype (torch.dtype): requested dtype (e.g. torch.uint8) for output Tensor

    Returns:
       Tensor with requested dtype (e.g. torch.uint8), note the quantization parameters
       are not stored in the Tensor, we are storing them in function arguments instead
    """
    assert input.dtype == torch.float32, f"Expecting input to have dtype torch.float32, but got dtype: {input.dtype}"
    _quant_min_max_bounds_check(quant_min, quant_max, dtype)

    inv_scale = 1.0 / scale
    return torch.clamp(torch.round(input * inv_scale) + zero_point, quant_min, quant_max).to(dtype)

quantized_decomposed_lib.define(
    "quantize_per_tensor.tensor(Tensor input, Tensor scale, Tensor zero_point, "
    "int quant_min, int quant_max, ScalarType dtype) -> Tensor")

@impl(quantized_decomposed_lib, "quantize_per_tensor.tensor", "CompositeExplicitAutograd")
def quantize_per_tensor_tensor(
        input: torch.Tensor,
        scale: torch.Tensor,
        zero_point: torch.Tensor,
        quant_min: int,
        quant_max: int,
        dtype: torch.dtype
) -> torch.Tensor:
    """ Affine quantization for the Tensor using the same quantization parameters to map
    from floating point to quantized values
    Same as `quantize_per_tensor` but scale and zero_point are Scalar Tensor instead of
    scalar values
    """
    assert zero_point.numel() == 1, f"Exepecting zero_point tensor to be one element, but received : {zero_point.numel()}"
    assert scale.numel() == 1, f"Exepecting scale tensor to be one element, but received : {scale.numel()}"
    return quantize_per_tensor(input, scale.item(), zero_point.item(), quant_min, quant_max, dtype)

@impl(quantized_decomposed_lib, "quantize_per_tensor.tensor", "Meta")
def quantize_per_tensor_tensor_meta(input, scale, zero_point, quant_min, quant_max, dtype):
    assert zero_point.numel() == 1, f"Exepecting zero_point tensor to be one element, but received : {zero_point.numel()}"
    assert scale.numel() == 1, f"Exepecting scale tensor to be one element, but received : {scale.numel()}"
    assert input.dtype == torch.float32, f"Expecting input to have dtype torch.float32, but got dtype: {input.dtype}"
    _quant_min_max_bounds_check(quant_min, quant_max, dtype)
    return torch.empty_like(input, dtype=dtype)

# Note: quant_min/quant_max/dtype are not used in the operator, but for now it's kept in
# the signature as metadata for the input Tensor, this might be useful for pattern
# matching in the future
# We will revisit this later if we found there are no use cases for it
quantized_decomposed_lib.define(
    "dequantize_per_tensor(Tensor input, float scale, int zero_point, "
    "int quant_min, int quant_max, ScalarType dtype) -> Tensor")

@impl(quantized_decomposed_lib, "dequantize_per_tensor", "CompositeExplicitAutograd")
def dequantize_per_tensor(
        input: torch.Tensor,
        scale: float,
        zero_point: int,
        quant_min: int,
        quant_max: int,
        dtype: torch.dtype
) -> torch.Tensor:
    """ Affine dequantization for the Tensor using the same quantization parameters to map
    from quantized values to floating point values

    Args:
       input (torch.Tensor): Tensor with dtype matching `dtype` argument,
       e.g. (`torch.uint8`), it is a per tensor quantized Tensor if combined with
       quantization parameters in the argument of this function (scale/zero_point)

       scale (float): quantization parameter for affine quantization

       zero_point (int): quantization parameter for affine quantization

       quant_min (int): minimum quantized value for input Tensor (not used in computation,
       reserved for pattern matching)

       quant_max (int): maximum quantized value for input Tensor (not used in computation,
       reserved for pattern matching)

       dtype (torch.dtype): dtype for input Tensor (not used in computation,
       reserved for pattern matching)

    Returns:
       dequantized float32 Tensor
    """
    assert input.dtype == dtype, f"Expecting input to have dtype: {dtype}"
    if dtype in [torch.uint8, torch.int8, torch.int32]:
        # TODO: investigate why
        # (input - zero_point).to(torch.float32) * scale
        # failed the test
        return (input.to(torch.float32) - zero_point) * scale
    else:
        raise ValueError(f"Unsupported dtype in dequantize_per_tensor: {dtype}")


quantized_decomposed_lib.define(
    "dequantize_per_tensor.tensor(Tensor input, Tensor scale, Tensor zero_point, "
    "int quant_min, int quant_max, ScalarType dtype) -> Tensor")

@impl(quantized_decomposed_lib, "dequantize_per_tensor.tensor", "CompositeExplicitAutograd")
def dequantize_per_tensor_tensor(
        input: torch.Tensor,
        scale: torch.Tensor,
        zero_point: torch.Tensor,
        quant_min: int,
        quant_max: int,
        dtype: torch.dtype
) -> torch.Tensor:
    """ Affine dequantization for the Tensor using the same quantization parameters to map
    from quantized values to floating point values
    Same as `dequantize_per_tensor` but scale and zero_point are Scalar Tensor instead of
    scalar values
    """
    assert zero_point.numel() == 1, f"Exepecting zero_point tensor to be one element, but received : {zero_point.numel()}"
    assert scale.numel() == 1, f"Exepecting scale tensor to be one element, but received : {scale.numel()}"
    return dequantize_per_tensor(input, scale.item(), zero_point.item(), quant_min, quant_max, dtype)

@impl(quantized_decomposed_lib, "dequantize_per_tensor.tensor", "Meta")
def dequantize_per_tensor_tensor_meta(input, scale, zero_point, quant_min, quant_max, dtype):
    assert zero_point.numel() == 1, f"Exepecting zero_point tensor to be one element, but received : {zero_point.numel()}"
    assert scale.numel() == 1, f"Exepecting scale tensor to be one element, but received : {scale.numel()}"
    assert input.dtype == dtype, f"Expecting input to have dtype: {dtype}"
    if dtype in [torch.uint8, torch.int8, torch.int32]:
        return torch.empty_like(input, dtype=torch.float32)
    else:
        raise ValueError(f"Unsupported dtype in dequantize_per_tensor: {dtype}")


quantized_decomposed_lib.define(
    "conv_unary_inductor.tensor(Tensor qx, Tensor input_scale, Tensor input_zero_point,"
    "Tensor qw, Tensor weight_scale, Tensor weight_zero_point, int w_axis, Tensor? bias,"
    "int[] stride, int[] padding, int[] dilation, int groups, Tensor output_scale, Tensor output_zero_point,"
    "str unary_post_op) -> Tensor")
@impl(quantized_decomposed_lib, "conv_unary_inductor.tensor", "CPU")
def conv_unary_inductor(qx, x_scale, x_zp, qw, w_scale, w_zp, w_axis, 
                        bias, stride, padding, dilation, groups, output_scale,
                        output_zero_point, unary_post_op):
    quantized = torch.ops.quantized

    if unary_post_op in ['relu', 'relu_']:
        return quantized.conv_relu_int8_cpu_tensor(
            qx, x_scale, x_zp, qw, w_scale, w_zp, bias,
            stride, padding, dilation, groups, output_scale, output_zero_point
        )
    # Last case: unary_post_op = 'None'
    return quantized.conv_int8_cpu_tensor(
        qx, x_scale, x_zp, qw, w_scale, w_zp, bias,
        stride, padding, dilation, groups, output_scale, output_zero_point
    )

@impl(quantized_decomposed_lib, "conv_unary_inductor.tensor", "MkldnnCPU")
def conv_unary_inductor(qx, x_scale, x_zp, qw, w_scale, w_zp, w_axis, 
                         bias, stride, padding, dilation, groups, output_scale,
                         output_zero_point, unary_post_op):
    quantized = torch.ops.quantized

    if unary_post_op in ['relu', 'relu_']:
        return quantized.conv_relu_int8_packed_weight(
            qx, x_scale, x_zp, qw, w_scale, w_zp, bias,
            stride, padding, dilation, groups, output_scale, output_zero_point
        )
    # Last case: unary_post_op = 'None'
    return quantized.conv_int8_packed_weight(
        qx, x_scale, x_zp, qw, w_scale, w_zp, bias,
        stride, padding, dilation, groups, output_scale, output_zero_point
    )

@impl(quantized_decomposed_lib, "conv_unary_inductor.tensor", "Meta")
def conv_unary_inductor(qx, x_scale, x_zp, qw, w_scale, w_zp, w_axis, bias,
                        stride, padding, dilation, groups, output_scale,
                        output_zero_point, unary_post_op):
    if len(qx.shape) == 3 and len(qw.shape) == 4:
        # For conv1d, x and w should both have rank 3
        # But if weight is prepacked, it's rank is 4 by unsqueeze(2)
        qw_squeezed = torch.squeeze(qw, 2)
    else:
        qw_squeezed = qw
    shape_out = calc_conv_nd_return_shape(
        qx,
        qw_squeezed,
        stride,
        padding,
        dilation,
        False,
        groups,
        None,
    )
    out_format = torch.channels_last
    if len(shape_out) == 5:
        out_format = torch.channels_last_3d
    out = qx.new_empty(shape_out)
    if len(shape_out) == 3:
        out = out.unsqueeze(2)
    out = out.to(memory_format=out_format)
    if len(shape_out) == 3:
        out = out.squeeze(2)
    return out

quantized_decomposed_lib.define(
    "linear_unary_inductor.tensor(Tensor qx, Tensor input_scale, Tensor input_zero_point,"
    "Tensor qw, Tensor weight_scale, Tensor weight_zero_point, int w_axis, Tensor? bias,"
    "Tensor output_scale, Tensor output_zero_point, str unary_post_op) -> Tensor")

@impl(quantized_decomposed_lib, "linear_unary_inductor.tensor", "CPU")
def linear_unary_cpu(qx, x_scale, x_zp, qw, w_scale, w_zp, w_axis,
                     bias, output_scale, output_zero_point, unary_post_op):
    quantized = torch.ops.quantized

    qx = torch._make_per_tensor_quantized_tensor(qx, x_scale, x_zp)
    qw = torch._make_per_channel_quantized_tensor(qw, w_scale, w_zp, w_axis)
    w_packed = quantized.linear_prepack(qw, bias)

    if unary_post_op == 'relu':
        return quantized.linear_relu(qx, w_packed, output_scale, output_zero_point)
    # Last case: unary_post_op = 'None'
    return quantized.linear(qx, w_packed, output_scale, output_zero_point)

@impl(quantized_decomposed_lib, "linear_unary_inductor.tensor", "MkldnnCPU")
def linear_unary_mkldnn_cpu(qx, x_scale, x_zp, qw, w_scale, w_zp, w_axis,
                            bias, output_scale, output_zero_point, unary_post_op):
    quantized = torch.ops.quantized

    if unary_post_op == 'relu':
        return quantized.linear_relu_int8_packed_weight(
            qx, x_scale, x_zp, qw, w_scale, w_zp, bias, output_scale, output_zero_point
        )
    # Last case: unary_post_op = 'None'
    return quantized.linear_int8_packed_weight(
        qx, x_scale, x_zp, qw, w_scale, w_zp, bias, output_scale, output_zero_point
    )

@impl(quantized_decomposed_lib, "linear_unary_inductor.tensor", "Meta")
def linear_unary_meta(qx, x_scale, x_zp, qw, w_scale, w_zp, w_axis, bias,
                      output_scale, output_zero_point, unary_post_op):
    # Original shapes:
    # qx = [BS, IC], qw = [OC, IC], out = [BS, OC]
    # After weight prepacking:
    # qx = [BS, IC], qw = [IC, OC], out = [BS, OC]
    # Assume weight is prepacked
    shape_out = list(qx.shape)
    shape_out[-1] = qw.shape[-1]
    out_format = torch.contiguous_format
    out = qx.new_empty(tuple(shape_out))
    out = out.to(memory_format=out_format)
    return out

quantized_decomposed_lib.define(
    "choose_qparams.tensor(Tensor input, int quant_min, int quant_max, "
    "ScalarType dtype) -> (Tensor, Tensor)")

@impl(quantized_decomposed_lib, "choose_qparams.tensor", "CompositeExplicitAutograd")
def choose_qparams_tensor(
        input: torch.Tensor,
        qmin: int,
        qmax: int,
        dtype: torch.dtype
) -> Tuple[torch.Tensor, torch.Tensor]:
    """ Given an input Tensor, derive the per tensor affine quantization parameter
    (scale and zero_point) for target quantized Tensor from the Tensor

    Args:
       input (torch.Tensor): floating point input Tensor
       quant_min (int): minimum quantized value for target quantized Tensor
       quant_max (int): maximum quantized value for target quantized Tensor
       dtype (torch.dtype): dtype for target quantized Tensor

    Returns:
       scale (float): quantization parameter for the target quantized Tensor
       zero_point (int): quantization parameter for the target quantized Tensor
    """
    assert input.dtype == torch.float32, f"Expecting input to have dtype torch.float32, but got dtype: {input.dtype}"
    assert dtype == torch.int8 or dtype == torch.uint8 or dtype == torch.int32, \
        f"Expecting target dtype to be int8 uint8 or int32, but got: {dtype}"
    validate_qmin_qmax(qmin, qmax)

    min_val, max_val = torch.aminmax(input)

    return determine_qparams(
        min_val, max_val, qmin, qmax, dtype, torch.Tensor([torch.finfo(torch.float32).eps]), has_customized_qrange=False)

quantized_decomposed_lib.define(
    "choose_qparams_symmetric.tensor(Tensor input, int quant_min, int quant_max, "
    "ScalarType dtype) -> (Tensor, Tensor)")

@impl(quantized_decomposed_lib, "choose_qparams_symmetric.tensor", "CompositeExplicitAutograd")
def choose_qparams_symmetric_tensor(
        input: torch.Tensor,
        qmin: int,
        qmax: int,
        dtype: torch.dtype
) -> Tuple[torch.Tensor, torch.Tensor]:
    """ Given an input Tensor, derive the per tensor affine quantization parameter
    (scale and zero_point) for target quantized Tensor from the Tensor

    Args:
       input (torch.Tensor): floating point input Tensor
       quant_min (int): minimum quantized value for target quantized Tensor
       quant_max (int): maximum quantized value for target quantized Tensor
       dtype (torch.dtype): dtype for target quantized Tensor

    Returns:
       scale (float): quantization parameter for the target quantized Tensor
       zero_point (int): quantization parameter for the target quantized Tensor
    """
    assert input.dtype == torch.float32, f"Expecting input to have dtype torch.float32, but got dtype: {input.dtype}"
    assert dtype == torch.int8 or dtype == torch.uint8 or dtype == torch.int32, \
        f"Expecting target dtype to be int8 uint8 or int32, but got: {dtype}"
    validate_qmin_qmax(qmin, qmax)

    min_val, max_val = torch.aminmax(input)
    return determine_qparams(
        min_val,
        max_val,
        qmin,
        qmax,
        dtype,
        torch.Tensor([torch.finfo(torch.float32).eps]),
        has_customized_qrange=False,
        qscheme=torch.per_tensor_symmetric
    )

@impl(quantized_decomposed_lib, "choose_qparams.tensor", "Meta")
def choose_qparams_tensor_meta(
        input: torch.Tensor,
        quant_min: int,
        quant_max: int,
        dtype: torch.dtype
) -> Tuple[torch.Tensor, torch.Tensor]:
    assert input.dtype == torch.float32, f"Expecting input to have dtype torch.float32, but got dtype: {input.dtype}"
    assert quant_min < quant_max, f"Expecting quant_min to be smaller than quant_max but received min: \
        {quant_min} max: {quant_max}"
    return torch.empty(1, dtype=torch.float, device=input.device), torch.empty(1, dtype=torch.int32, device=input.device)

@impl(quantized_decomposed_lib, "choose_qparams_symmetric.tensor", "Meta")
def choose_qparams_symmetric_tensor_meta(
        input: torch.Tensor,
        quant_min: int,
        quant_max: int,
        dtype: torch.dtype
) -> Tuple[torch.Tensor, torch.Tensor]:
    return torch.empty(1, dtype=torch.float, device=input.device), torch.empty(1, dtype=torch.int32, device=input.device)
# Helper function used to implement per-channel quantization against any axis
def _permute_to_axis_zero(x, axis):
    new_axis_list = list(range(x.dim()))
    new_axis_list[axis] = 0
    new_axis_list[0] = axis
    y = x.permute(tuple(new_axis_list))
    return y, new_axis_list

quantized_decomposed_lib.define(
    "quantize_per_channel(Tensor input, Tensor scales, Tensor zero_points, int axis, "
    "int quant_min, int quant_max, ScalarType dtype) -> Tensor")

@impl(quantized_decomposed_lib, "quantize_per_channel", "CompositeExplicitAutograd")
def quantize_per_channel(
        input: torch.Tensor,
        scales: torch.Tensor,
        zero_points: torch.Tensor,
        axis: int,
        quant_min: int,
        quant_max: int,
        dtype: torch.dtype
) -> torch.Tensor:
    """ Affine per channel quantization for the Tensor using the same quantization
    parameters for each channel/axis to map from floating point to quantized values

    Args:
       input (torch.Tensor): original float32 Tensor
       scales (torch.Tensor): a list of scale quantization parameter for
       affine quantization, one per channel
       zero_point (torch.Tensor): a list of zero_point quantization parameter for
       affine quantization, one per channel
       quant_min (int): minimum quantized value for output Tensor
       quant_max (int): maximum quantized value for output Tensor
       dtype (torch.dtype): requested dtype (e.g. torch.uint8) for output Tensor

    Returns:
       Tensor with requested dtype (e.g. torch.uint8), note the quantization parameters
       are not stored in the Tensor, we are storing them in function arguments instead
    """
    assert input.dtype == torch.float32, f"Expecting input to have dtype torch.float32, but got dtype: {input.dtype}"
    assert axis < input.dim(), f"Expecting axis to be < {input.dim()}"
    _quant_min_max_bounds_check(quant_min, quant_max, dtype)
    input, permute_axis_list = _permute_to_axis_zero(input, axis)
    res = torch.zeros_like(input)

    for i in range(input.size(0)):
        res[i] = torch.clamp(
            torch.round(input[i] * (1.0 / scales[i])) + zero_points[i],
            quant_min,
            quant_max
        )

    out = res.permute(tuple(permute_axis_list))
    return out.to(dtype)

@impl(quantized_decomposed_lib, "quantize_per_channel", "Meta")
def quantize_per_channel_meta(
        input: torch.Tensor,
        scales: torch.Tensor,
        zero_points: torch.Tensor,
        axis: int,
        quant_min: int,
        quant_max: int,
        dtype: torch.dtype
) -> torch.Tensor:
    assert input.dtype == torch.float32, f"Expecting input to have dtype torch.float32, but got dtype: {input.dtype}"
    assert axis < input.dim(), f"Expecting axis to be < {input.dim()}"
    _quant_min_max_bounds_check(quant_min, quant_max, dtype)
    return torch.empty_like(input, dtype=dtype)

# Note: quant_min/quant_max/dtype are not used in the operator, but for now it's kept in
# the signature as metadata for the input Tensor, this might be useful for pattern
# matching in the future
# We will revisit this later if we found there are no use cases for it
quantized_decomposed_lib.define(
    "dequantize_per_channel(Tensor input, Tensor scales, Tensor zero_points, int axis, "
    "int quant_min, int quant_max, ScalarType dtype) -> Tensor")

@impl(quantized_decomposed_lib, "dequantize_per_channel", "CompositeExplicitAutograd")
def dequantize_per_channel(
        input: torch.Tensor,
        scales: torch.Tensor,
        zero_points: torch.Tensor,
        axis: int,
        quant_min: int,
        quant_max: int,
        dtype: torch.dtype
) -> torch.Tensor:
    """ Affine per channel dequantization for the Tensor using the same quantization
    parameters for each channel/axis to map from quantized values to floating point values

    Args:
       input (torch.Tensor): Tensor with dtype matching `dtype` argument,
       e.g. (`torch.uint8`), it is a per channel quantized Tensor if combined with
       quantization parameter in the argument of this function (scales/zero_points/axis)

       scales (torch.Tensor): a list of scale quantization parameter for
       affine quantization, one per channel

       zero_points (torch.Tensor): a list of zero_point quantization parameter for
       affine quantization, one per channel

       quant_min (int): minimum quantized value for output Tensor (not used in computation,
       reserved for pattern matching)

       quant_max (int): maximum quantized value for output Tensor (not used in computation,
       reserved for pattern matching)

       dtype (torch.dtype): requested dtype for output Tensor (not used in computation,
       reserved for pattern matching)

    Returns:
       dequantized float32 Tensor
    """
    assert input.dtype == dtype, f"Expecting input to have dtype {dtype}, but got dtype: {input.dtype}"
    assert axis < input.dim(), f"Expecting axis to be < {input.dim()}"
    _quant_min_max_bounds_check(quant_min, quant_max, dtype)
    input, permute_axis_list = _permute_to_axis_zero(input, axis)
    res = torch.zeros_like(input, dtype=torch.float32)

    for i in range(input.size(0)):
        # TODO: investigate why
        # (input[i] - zero_points[i]).to(torch.float32) * scales[i]
        # failed the test
        res[i] = (input[i].to(torch.float32) - zero_points[i]) * scales[i]

    out = res.permute(tuple(permute_axis_list))
    return out

@impl(quantized_decomposed_lib, "dequantize_per_channel", "Meta")
def dequantize_per_channel_meta(
        input: torch.Tensor,
        scales: torch.Tensor,
        zero_points: torch.Tensor,
        axis: int,
        quant_min: int,
        quant_max: int,
        dtype: torch.dtype
) -> torch.Tensor:
    assert input.dtype == dtype, f"Expecting input to have dtype {dtype}, but got dtype: {input.dtype}"
    assert axis < input.dim(), f"Expecting axis to be < {input.dim()}"
    _quant_min_max_bounds_check(quant_min, quant_max, dtype)
    return torch.empty_like(input, dtype=torch.float32)
