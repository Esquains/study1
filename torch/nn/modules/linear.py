import math

import torch
from torch.nn.parameter import Parameter
from torch.nn import init
from .. import functional as F
from .module import Module


class Linear(Module):
    r"""Applies a linear transformation to the incoming data: :math:`y = Ax + b`

    Args:
        in_features: size of each input sample
        out_features: size of each output sample
        bias: If set to False, the layer will not learn an additive bias. Default: True
        initializer: dictionary of initializer of weights and bias. Default: dict()

    Shape:
        - Input: :math:`(N, in\_features)`
        - Output: :math:`(N, out\_features)`

    Attributes:
        weight: the learnable weights of the module of shape (out_features x in_features)
        bias:   the learnable bias of the module of shape (out_features)

    Examples::

        >>> m = nn.Linear(20, 30, initializer=lambda x: init.xavier_normal(x, 1))
        >>> m = nn.Linear(20, 30, initializer={"weight": lambda x: init.xavier_normal(x, 1)})
        >>> input = autograd.Variable(torch.randn(128, 20))
        >>> output = m(input)
        >>> print(output.size())
    """

    def __init__(self, in_features, out_features, bias=True, initializer=None):
        super(Linear, self).__init__()
        self.in_features = in_features
        self.out_features = out_features
        self.weight = Parameter(torch.Tensor(out_features, in_features))

        self.initializer = {"weight": self._initializer} if initializer is None else initializer
        if bias:
            self.bias = Parameter(torch.Tensor(out_features))
            if self.initializer.get("bias") is None:
                self.initializer["bias"] = self._initializer
        else:
            self.register_parameter('bias', None)
        self.reset_parameters()

    def reset_parameters(self):

        weight_initializer = self.initializer.get("weight")
        bias_initializer = self.initializer.get("bias")

        weight_initializer(self.weight)
        if self.bias is not None:
            bias_initializer(self.bias)

    def forward(self, input):
        if self.bias is None:
            return self._backend.Linear.apply(input, self.weight)
        else:
            return self._backend.Linear.apply(input, self.weight, self.bias)

    def __repr__(self):
        return self.__class__.__name__ + ' (' \
            + str(self.in_features) + ' -> ' \
            + str(self.out_features) + ')'

    def _initializer(self, x):
        stdv = 1. / math.sqrt(self.weight.size(1))
        return init.uniform(x, -stdv, stdv)


class Bilinear(Module):
    r"""Applies a bilinear transformation to the incoming data: :math:`y = x_1 * A * x_2 + b`

    Args:
        in1_features: size of each first input sample
        in2_features: size of each second input sample
        out_features: size of each output sample
        bias: If set to False, the layer will not learn an additive bias. Default: True

    Shape:
        - Input: :math:`(N, in1\_features)`, :math:`(N, in2\_features)`
        - Output: :math:`(N, out\_features)`

    Attributes:
        weight: the learnable weights of the module of shape (out_features x in1_features x in2_features)
        bias:   the learnable bias of the module of shape (out_features)

    Examples::

        >>> m = nn.Bilinear(20, 30, 40)
        >>> input1 = autograd.Variable(torch.randn(128, 20))
        >>> input1 = autograd.Variable(torch.randn(128, 30))
        >>> output = m(input1, input2)
        >>> print(output.size())
    """

    def __init__(self, in1_features, in2_features, out_features, bias=True):
        super(Bilinear, self).__init__()
        self.in1_features = in1_features
        self.in2_features = in2_features
        self.out_features = out_features
        self.weight = Parameter(torch.Tensor(out_features, in1_features, in2_features))

        if bias:
            self.bias = Parameter(torch.Tensor(out_features))
        else:
            self.register_parameter('bias', None)
        self.reset_parameters()

    def reset_parameters(self):
        stdv = 1. / math.sqrt(self.weight.size(1))
        self.weight.data.uniform_(-stdv, stdv)
        if self.bias is not None:
            self.bias.data.uniform_(-stdv, stdv)

    def forward(self, input1, input2):
        return F.bilinear(input1, input2, self.weight, self.bias)

    def __repr__(self):
        return self.__class__.__name__ + ' (' \
            + 'in1_features=' + str(self.in1_features) \
            + ', in2_features=' + str(self.in2_features) \
            + ', out_features=' + str(self.out_features) + ')'

# TODO: PartialLinear - maybe in sparse?
