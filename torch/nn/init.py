import math
import types
import random

import torch

##########################
# base initializer
##########################


class Initializer(object):
    """
    Base class for all initializations.
    """
    def __new__(cls, *non_tensor_args, **non_tensor_kwargs):
        first = None
        if len(non_tensor_args) > 0:
            first = non_tensor_args[0]
        if first is None:
            return object.__new__(cls)
        if isinstance(first, (torch.Tensor, torch.autograd.Variable)):
            return cls(*non_tensor_args[1:], **non_tensor_kwargs)(first)
        else:
            return object.__new__(cls)

##########################
# initializers
##########################


class Ones(Initializer):
    r"""Fills the input Tensor with ones.

    Args:
        tensor: an n-dimensional `torch.Tensor`

    Examples:
        >>> w = torch.Tensor(3, 5)
        >>> nn.init.ones(w)
        >>> ones_init = nn.init.ones()
        >>> ones_init(w)
    """
    def __call__(self, tensor):
        with torch.no_grad():
            return tensor.fill_(1)


class Zeros(Initializer):
    r"""Fills the input Tensor with zeros.

    Args:
        tensor: an n-dimensional `torch.Tensor`

    Examples:
        >>> w = torch.Tensor(3, 5)
        >>> nn.init.zeros(w)
        >>> zeros_init = nn.init.zeros()
        >>> zeros_init(w)
    """
    def __call__(self, tensor):
        with torch.no_grad():
            return tensor.fill_(0)


class Constant(Initializer):
    r"""Fills the input Tensor with the value :math:`\text{val}`.

    Args:
        tensor: an n-dimensional `torch.Tensor`
        val: the value to fill the tensor with

    Examples:
        >>> w = torch.Tensor(3, 5)
        >>> nn.init.constant(w, 0.3)
        >>> const_init = nn.init.constant(0.3)
        >>> const_init(w)
    """
    def __init__(self, val):
        self.val = val

    def __call__(self, tensor):
        with torch.no_grad():
            return tensor.fill_(self.val)


class Eye(Initializer):
    r"""Fills the 2-dimensional input `Tensor` with the identity
    matrix. Preserves the identity of the inputs in `Linear` layers, where as
    many inputs are preserved as possible.

    Args:
        tensor: a 2-dimensional `torch.Tensor`

    Examples:
        >>> w = torch.Tensor(3, 5)
        >>> nn.init.eye(w)
        >>> eye_init = nn.init.eye()
        >>> eye_init(w)
    """
    def __call__(self, tensor):
        if tensor.ndimension() != 2:
            raise ValueError("Only tensors with 2 dimensions are supported")

        with torch.no_grad():
            torch.eye(*tensor.shape, out=tensor)
        return tensor


class Orthogonal(Initializer):
    r"""Fills the input `Tensor` with a (semi) orthogonal matrix, as
    described in "Exact solutions to the nonlinear dynamics of learning in deep
    linear neural networks" - Saxe, A. et al. (2013). The input tensor must have
    at least 2 dimensions, and for tensors with more than 2 dimensions the
    trailing dimensions are flattened.

    Args:
        tensor: an n-dimensional `torch.Tensor`, where :math:`n \geq 2`
        gain: optional scaling factor

    Examples:
        >>> w = torch.Tensor(3, 5)
        >>> nn.init.orthogonal(w)
        >>> orth_init = nn.init.orthogonal(5)
        >>> orth_init(w)
    """
    def __init__(self, gain=1):
        self.gain = gain

    def __call__(self, tensor):
        if tensor.ndimension() < 2:
            raise ValueError("Only tensors with 2 or more dimensions are supported")

        rows = tensor.size(0)
        cols = tensor[0].numel()
        flattened = tensor.new(rows, cols).normal_(0, 1)

        if rows < cols:
            flattened.t_()

        q, r = torch.qr(flattened)
        d = torch.diag(r, 0)
        ph = d.sign()
        q *= ph

        if rows < cols:
            q.t_()

        with torch.no_grad():
            tensor.view_as(q).copy_(q)
            tensor.mul_(self.gain)
        return tensor


class Uniform(Initializer):
    r"""Fills the input Tensor with values drawn from the uniform
    distribution :math:`\mathcal{U}(a, b)`.

    Args:
        tensor: an n-dimensional `torch.Tensor`
        a: the lower bound of the uniform distribution
        b: the upper bound of the uniform distribution

    Examples:
        >>> w = torch.Tensor(3, 5)
        >>> nn.init.uniform(w, 4, 5)
        >>> uniform_init = nn.init.uniform(4, 5)
        >>> uniform_init(w)
    """
    def __init__(self, a=0, b=1):
        self.a = a
        self.b = b

    def __call__(self, tensor):
        with torch.no_grad():
            return tensor.uniform_(self.a, self.b)


class Normal(Initializer):
    r"""Fills the input Tensor with values drawn from the normal
    distribution :math:`\mathcal{N}(\text{mean}, \text{std})`.

    Args:
        tensor: an n-dimensional `torch.Tensor`
        mean: the mean of the normal distribution
        std: the standard deviation of the normal distribution

    Examples:
        >>> w = torch.Tensor(3, 5)
        >>> nn.init.normal(w)
        >>> normal_init = nn.init.normal()
        >>> normal_init(w)
    """
    def __init__(self, mean=0, std=1):
        self.mean = mean
        self.std = std

    def __call__(self, tensor):
        with torch.no_grad():
            return tensor.normal_(self.mean, self.std)


class Dirac(Initializer):
    r"""Fills the {3, 4, 5}-dimensional input `Tensor` with the Dirac
    delta function. Preserves the identity of the inputs in `Convolutional`
    layers, where as many input channels are preserved as possible.

    Args:
        tensor: a {3, 4, 5}-dimensional `torch.Tensor`

    Examples:
        >>> w = torch.Tensor(3, 16, 5, 5)
        >>> nn.init.dirac(w)
        >>> dirac_init = nn.init.dirac()
        >>> dirac_init(w)
    """
    def __call__(self, tensor):
        dimensions = tensor.ndimension()
        if dimensions not in [3, 4, 5]:
            raise ValueError("Only tensors with 3, 4, or 5 dimensions are supported")

        sizes = tensor.size()
        min_dim = min(sizes[0], sizes[1])
        with torch.no_grad():
            tensor.zero_()

            for d in range(min_dim):
                if dimensions == 3:  # Temporal convolution
                    tensor[d, d, tensor.size(2) // 2] = 1
                elif dimensions == 4:  # Spatial convolution
                    tensor[d, d, tensor.size(2) // 2, tensor.size(3) // 2] = 1
                else:  # Volumetric convolution
                    tensor[d, d, tensor.size(2) // 2, tensor.size(3) // 2, tensor.size(4) // 2] = 1
        return tensor


class Sparse(Initializer):
    r"""Fills the 2D input `Tensor` as a sparse matrix, where the
    non-zero elements will be drawn from the normal distribution
    :math:`\mathcal{N}(0, 0.01)`, as described in "Deep learning via
    Hessian-free optimization" - Martens, J. (2010).

    Args:
        tensor: an n-dimensional `torch.Tensor`
        sparsity: The fraction of elements in each column to be set to zero
        std: the standard deviation of the normal distribution used to generate
            the non-zero values

    Examples:
        >>> w = torch.Tensor(3, 5)
        >>> nn.init.sparse(w, sparsity=0.1)
        >>> sparse_init = nn.init.sparse(sparsity=0.1)
        >>> sparse_init(w)
    """
    def __init__(self, sparsity, std=0.01):
        self.sparsity = sparsity
        self.std = std

    def __call__(self, tensor):
        if tensor.ndimension() != 2:
            raise ValueError("Only tensors with 2 dimensions are supported")

        rows, cols = tensor.shape
        num_zeros = int(math.ceil(rows * self.sparsity))

        with torch.no_grad():
            tensor.normal_(0, self.std)
            for col_idx in range(cols):
                row_indices = list(range(rows))
                random.shuffle(row_indices)
                zero_indices = row_indices[:num_zeros]
                for row_idx in zero_indices:
                    tensor[row_idx, col_idx] = 0
        return tensor


class VarianceScaling(object):
    def __init__(self, gain, mode, distribution):
        mode = mode.lower()
        valid_modes = ["fan_in", "fan_avg", "fan_out"]
        if mode not in valid_modes:
            raise ValueError("Mode {} not supported, please use one of {}".format(mode, valid_modes))
        distribution = distribution.lower()
        valid_distributions = ["uniform", "normal"]
        if distribution not in valid_distributions:
            raise ValueError("Distribution {} not supported, please use one of {}".format(
                distribution, valid_distributions)
            )

        self.gain = gain
        self.mode = mode
        self.distribution = distribution

    def __call__(self, tensor):
        fan_in, fan_out = _calculate_fan_in_and_fan_out(tensor)
        scale = 1.0
        scale *= self.gain
        if self.mode == 'fan_in':
            scale /= max(1., fan_in)
        elif self.mode == 'fan_out':
            scale /= max(1., fan_out)
        else:
            scale /= max(1., float(fan_in + fan_out) / 2)

        if self.distribution == "uniform":
            bound = math.sqrt(3.0 * scale)
            with torch.no_grad():
                return tensor.uniform_(-bound, bound)
        else:
            std = math.sqrt(scale)
            with torch.no_grad():
                return tensor.normal_(0, std)


class XavierUniform(Initializer):
    r"""Fills the input `Tensor` with values according to the method
    described in "Understanding the difficulty of training deep feedforward
    neural networks" - Glorot, X. & Bengio, Y. (2010), using a uniform
    distribution. The resulting tensor will have values sampled from
    :math:`\mathcal{U}(-a, a)` where
    .. math::
        a = \text{gain} \times \sqrt{\frac{6}{\text{fan_in} + \text{fan_out}}}
    Also known as Glorot initialisation.

    Args:
        tensor: an n-dimensional `torch.Tensor`
        gain: an optional scaling factor

    Examples:
        >>> w = torch.Tensor(3, 5)
        >>> nn.init.xavier_uniform(w, gain=nn.init.calculate_gain('relu'))
        >>> xavier_init = nn.init.xavier_uniform(gain=nn.init.calculate_gain('relu'))
        >>> xavier_init(w)
    """
    def __init__(self, gain=1.):
        gain = gain ** 2
        self.vs = VarianceScaling(
            gain=gain,
            mode="fan_avg",
            distribution="uniform"
        )

    def __call__(self, tensor):
        return self.vs(tensor)


class XavierNormal(Initializer):
    r"""Fills the input `Tensor` with values according to the method
    described in "Understanding the difficulty of training deep feedforward
    neural networks" - Glorot, X. & Bengio, Y. (2010), using a normal
    distribution. The resulting tensor will have values sampled from
    :math:`\mathcal{N}(0, \text{std})` where
    .. math::
        \text{std} = \text{gain} \times \sqrt{\frac{2}{\text{fan_in} + \text{fan_out}}}
    Also known as Glorot initialisation.

    Args:
        tensor: an n-dimensional `torch.Tensor`
        gain: an optional scaling factor

    Examples:
        >>> w = torch.Tensor(3, 5)
        >>> nn.init.xavier_normal(w)
        >>> xavier_init = nn.init.xavier_normal(gain=nn.init.calculate_gain('relu'))
        >>> xavier_init(w)
    """
    def __init__(self, gain=1.):
        gain = gain ** 2
        self.vs = VarianceScaling(
            gain=gain,
            mode="fan_avg",
            distribution="normal"
        )

    def __call__(self, tensor):
        return self.vs(tensor)


class KaimingUniform(Initializer):
    r"""Fills the input `Tensor` with values according to the method
    described in "Delving deep into rectifiers: Surpassing human-level
    performance on ImageNet classification" - He, K. et al. (2015), using a
    uniform distribution. The resulting tensor will have values sampled from
    :math:`\mathcal{U}(-\text{bound}, \text{bound})` where
    .. math::
        \text{bound} = \sqrt{\frac{6}{(1 + a^2) \times \text{fan_in}}}
    Also known as He initialisation.

    Args:
        tensor: an n-dimensional `torch.Tensor`
        a: the negative slope of the rectifier used after this layer (0 for ReLU
            by default)
        mode: either 'fan_in' (default) or 'fan_out'. Choosing `fan_in`
            preserves the magnitude of the variance of the weights in the
            forward pass. Choosing `fan_out` preserves the magnitudes in the
            backwards pass.

    Examples:
        >>> w = torch.Tensor(3, 5)
        >>> nn.init.kaiming_uniform(w, mode='fan_in')
        >>> he_init = nn.init.kaiming_uniform(mode='fan_in')
        >>> he_init(w)
    """
    def __init__(self, a=0, mode="fan_in"):
        gain = calculate_gain('leaky_relu', a) ** 2
        self.vs = VarianceScaling(
            gain=gain,
            mode=mode,
            distribution="uniform"
        )

    def __call__(self, tensor):
        return self.vs(tensor)


class KaimingNormal(Initializer):
    r"""Fills the input `Tensor` with values according to the method
    described in "Delving deep into rectifiers: Surpassing human-level
    performance on ImageNet classification" - He, K. et al. (2015), using a
    normal distribution. The resulting tensor will have values sampled from
    :math:`\mathcal{N}(0, \text{std})` where
    .. math::
        \text{std} = \sqrt{\frac{2}{(1 + a^2) \times \text{fan_in}}}
    Also known as He initialisation.

    Args:
        tensor: an n-dimensional `torch.Tensor`
        a: the negative slope of the rectifier used after this layer (0 for ReLU
            by default)
        mode: either 'fan_in' (default) or 'fan_out'. Choosing `fan_in`
            preserves the magnitude of the variance of the weights in the
            forward pass. Choosing `fan_out` preserves the magnitudes in the
            backwards pass.

    Examples:
        >>> w = torch.Tensor(3, 5)
        >>> nn.init.kaiming_normal(w, mode='fan_out')
        >>> he_init = nn.init.kaiming_normal(mode='fan_out')
        >>> he_init(w)
    """
    def __init__(self, a=0, mode="fan_in"):
        gain = calculate_gain('leaky_relu', a) ** 2
        self.vs = VarianceScaling(
            gain=gain,
            mode=mode,
            distribution="normal"
        )

    def __call__(self, tensor):
        return self.vs(tensor)

##########################
# aliases
##########################


ones = Ones
zeros = Zeros
constant = Constant
eye = Eye
orthogonal = Orthogonal
uniform = Uniform
normal = Normal
dirac = Dirac
sparse = Sparse
xavier_uniform = XavierUniform
xavier_normal = XavierNormal
kaiming_uniform = KaimingUniform
kaiming_normal = KaimingNormal

##########################
# utility functions
##########################


def calculate_gain(nonlinearity, param=None):
    """Return the recommended gain value for the given nonlinearity function.
    The values are as follows:

    ============ ==========================================
    nonlinearity gain
    ============ ==========================================
    linear       :math:`1`
    conv{1,2,3}d :math:`1`
    sigmoid      :math:`1`
    selu         :math:`1`
    tanh         :math:`5.0 / 3`
    relu         :math:`\sqrt{2}`
    leaky_relu   :math:`\sqrt{2 / (1 + negative\_slope^2)}`
    ============ ==========================================

    Args:
        nonlinearity: the nonlinear function (`nn.functional` name)
        param: optional parameter for the nonlinear function

    Examples:
        >>> gain = nn.init.calculate_gain('leaky_relu')
    """
    linear_fns = ['linear', 'conv1d', 'conv2d', 'conv3d', 'conv_transpose1d', 'conv_transpose2d', 'conv_transpose3d']
    non_linear_fns = ['sigmoid', 'selu']
    if nonlinearity in linear_fns or nonlinearity in non_linear_fns:
        return 1.
    elif nonlinearity == 'tanh':
        return 5.0 / 3
    elif nonlinearity == 'relu':
        return math.sqrt(2.0)
    elif nonlinearity == 'leaky_relu':
        if param is None:
            negative_slope = 0.01
        elif not isinstance(param, bool) and isinstance(param, int) or isinstance(param, float):
            # True/False are instances of int, hence check above
            negative_slope = param
        else:
            raise ValueError("negative_slope {} not a valid number".format(param))
        return math.sqrt(2.0 / (1 + negative_slope ** 2))
    else:
        raise ValueError("Unsupported nonlinearity {}".format(nonlinearity))


def _calculate_fan_in_and_fan_out(tensor):
    dimensions = tensor.ndimension()
    if dimensions < 2:
        raise ValueError("Fan in and fan out can not be computed for tensor with less than 2 dimensions")

    if dimensions == 2:  # Linear
        fan_in = tensor.size(1)
        fan_out = tensor.size(0)
    else:
        num_input_fmaps = tensor.size(1)
        num_output_fmaps = tensor.size(0)
        receptive_field_size = 1
        if tensor.dim() > 2:
            receptive_field_size = tensor[0][0].numel()
        fan_in = num_input_fmaps * receptive_field_size
        fan_out = num_output_fmaps * receptive_field_size

    return fan_in, fan_out
