from numbers import Number

import math

import torch
from torch.distributions.distribution import Distribution
from torch.distributions.utils import broadcast_all


class Pareto(Distribution):
    r"""
    Samples from a Pareto Type 1 distribution.

    Example::

        >>> m = Pareto(torch.Tensor([1.0]), torch.Tensor([1.0]))
        >>> m.sample() # sample from a Pareto distribution with scale=1 and alpha=1
         1.5623
        [torch.FloatTensor of size 1]

    Args:
        scale (float or Tensor or Variable): Scale parameter of the distribution
        alpha (float or Tensor or Variable): Shape parameter of the distribution
    """
    has_rsample = True

    def __init__(self, scale, alpha):
        self.scale, self.alpha = broadcast_all(scale, alpha)
        if isinstance(scale, Number) and isinstance(alpha, Number):
            batch_shape = torch.Size()
        else:
            batch_shape = self.scale.size()
        super(Pareto, self).__init__(batch_shape)

    def rsample(self, sample_shape=torch.Size()):
        shape = self._extended_shape(sample_shape)
        exp_dist = self.alpha.new(shape).exponential_()
        return self.scale.new(shape) * torch.exp(exp_dist)

    def log_prob(self, value):
        self._validate_log_prob_arg(value)
        return torch.log(self.alpha / value) + self.alpha * (self.scale / value).log()

    def entropy(self):
        return ((self.scale / self.alpha).log() + (1 + self.alpha.reciprocal())) / math.log(2)
