r"""
The ``distributions`` package contains parameterizable probability distributions
and sampling functions.

Policy gradient methods can be implemented using the
:meth:`~torch.distributions.Distribution.log_prob` method, when the probability
density function is differentiable with respect to its parameters. A basic
method is the REINFORCE rule:

.. math::

    \Delta\theta  = \alpha r \frac{\partial\log p(a|\pi^\theta(s))}{\partial\theta}

where :math:`\theta` are the parameters, :math:`\alpha` is the learning rate,
:math:`r` is the reward and :math:`p(a|\pi^\theta(s))` is the probability of
taking action :math:`a` in state :math:`s` given policy :math:`\pi^\theta`.

In practice we would sample an action from the output of a network, apply this
action in an environment, and then use ``log_prob`` to construct an equivalent
loss function. Note that we use a negative because optimisers use gradient
descent, whilst the rule above assumes gradient ascent. With a categorical
policy, the code for implementing REINFORCE would be as follows::

    probs = policy_network(state)
    # NOTE: this is equivalent to what used to be called multinomial
    m = Categorical(probs)
    action = m.sample()
    next_state, reward = env.step(action)
    loss = -m.log_prob(action) * reward
    loss.backward()
"""
import math
from numbers import Number
import torch
from torch.autograd import Function, Variable
from torch.autograd.function import once_differentiable

__all__ = ['Distribution', 'Bernoulli', 'Categorical', 'Normal', 'Gamma']


def _expand_n(v, n):
    r"""
    Cleanly expand float or Tensor or Variable parameters.
    """
    if isinstance(v, Number):
        return torch.Tensor([v]).expand(n, 1)
    else:
        return v.expand(n, *v.size())


class Distribution(object):
    r"""
    Distribution is the abstract base class for probability distributions.
    """

    def sample(self):
        """
        Generates a single sample or single batch of samples if the distribution
        parameters are batched.
        """
        raise NotImplementedError

    def sample_n(self, n):
        """
        Generates n samples or n batches of samples if the distribution parameters
        are batched.
        """
        raise NotImplementedError

    def log_prob(self, value):
        """
        Returns the log of the probability density/mass function evaluated at
        `value`.

        Args:
            value (Tensor or Variable):
        """
        raise NotImplementedError


class Bernoulli(Distribution):
    r"""
    Creates a Bernoulli distribution parameterized by `probs`.

    Samples are binary (0 or 1). They take the value `1` with probability `p`
    and `0` with probability `1 - p`.

    Example::

        >>> m = Bernoulli(torch.Tensor([0.3]))
        >>> m.sample()  # 30% chance 1; 70% chance 0
         0.0
        [torch.FloatTensor of size 1]

    Args:
        probs (Tensor or Variable): the probabilty of sampling `1`
    """

    def __init__(self, probs):
        self.probs = probs

    def sample(self):
        return torch.bernoulli(self.probs)

    def sample_n(self, n):
        return torch.bernoulli(self.probs.expand(n, *self.probs.size()))

    def log_prob(self, value):
        # compute the log probabilities for 0 and 1
        log_pmf = (torch.stack([1 - self.probs, self.probs])).log()

        # evaluate using the values
        return log_pmf.gather(0, value.unsqueeze(0).long()).squeeze(0)


class Categorical(Distribution):
    r"""
    Creates a categorical distribution parameterized by `probs`.

    .. note::
        It is equivalent to the distribution that ``multinomial()`` samples from.

    Samples are integers from `0 ... K-1` where `K` is probs.size(-1).

    If `probs` is 1D with length-`K`, each element is the relative probability
    of sampling the class at that index.

    If `probs` is 2D, it is treated as a batch of probability vectors.

    See also: :func:`torch.multinomial`

    Example::

        >>> m = Categorical(torch.Tensor([ 0.25, 0.25, 0.25, 0.25 ]))
        >>> m.sample()  # equal probability of 0, 1, 2, 3
         3
        [torch.LongTensor of size 1]

    Args:
        probs (Tensor or Variable): event probabilities
    """

    def __init__(self, probs):
        if probs.dim() != 1 and probs.dim() != 2:
            # TODO: treat higher dimensions as part of the batch
            raise ValueError("probs must be 1D or 2D")
        self.probs = probs

    def sample(self):
        return torch.multinomial(self.probs, 1, True).squeeze(-1)

    def sample_n(self, n):
        if n == 1:
            return self.sample().expand(1, 1)
        else:
            return torch.multinomial(self.probs, n, True).t()

    def log_prob(self, value):
        p = self.probs / self.probs.sum(-1, keepdim=True)
        if value.dim() == 1 and self.probs.dim() == 1:
            # special handling until we have 0-dim tensor support
            return p.gather(-1, value).log()

        return p.gather(-1, value.unsqueeze(-1)).squeeze(-1).log()


class Normal(Distribution):
    r"""
    Creates a normal (also called Gaussian) distribution parameterized by
    `mean` and `std`.

    Example::

        >>> m = Normal(torch.Tensor([0.0]), torch.Tensor([1.0]))
        >>> m.sample()  # normally distributed with mean=0 and stddev=1
         0.1046
        [torch.FloatTensor of size 1]

    Args:
        mean (float or Tensor or Variable): mean of the distribution
        std (float or Tensor or Variable): standard deviation of the distribution
    """

    def __init__(self, mean, std):
        self.mean = mean
        self.std = std

    def sample(self):
        return torch.normal(self.mean, self.std)

    def sample_n(self, n):
        return torch.normal(_expand_n(self.mean, n), _expand_n(self.std, n))

    def log_prob(self, value):
        # compute the variance
        var = (self.std ** 2)
        log_std = math.log(self.std) if isinstance(self.std, Number) else self.std.log()
        return -((value - self.mean) ** 2) / (2 * var) - log_std - math.log(math.sqrt(2 * math.pi))


class _StandardGamma(Function):
    @staticmethod
    def forward(ctx, alpha):
        x = torch._C._standard_gamma(alpha)
        ctx.save_for_backward(x, alpha)
        return x

    @staticmethod
    @once_differentiable
    def backward(ctx, grad_output):
        x, alpha = ctx.saved_tensors
        grad = torch._C._standard_gamma_grad(x, alpha)
        return grad_output * grad


def _standard_gamma(alpha):
    if not isinstance(alpha, Variable):
        return torch._C._standard_gamma(alpha)
    if not alpha.requires_grad:
        return Variable(torch._C._standard_gamma(alpha.data))
    return _StandardGamma.apply(alpha)


class Gamma(Distribution):
    r"""
    Creates a Gamma distribution parameterized by shape `alpha` and rate `beta`.

    Example::

        >>> m = Gamma(torch.Tensor([1.0]), torch.Tensor([1.0]))
        >>> m.sample()  # Gamma distributed with shape alpha=1 and rate beta=1
         0.1046
        [torch.FloatTensor of size 1]

    Args:
        alpha (float or Tensor or Variable): shape parameter of the distribution
        beta (float or Tensor or Variable): rate = 1 / scale of the distribution
    """
    reparameterized = True

    def __init__(self, alpha, beta):
        # TODO handle (Variable, Number) cases
        alpha_num = isinstance(alpha, Number)
        beta_num = isinstance(beta, Number)
        if alpha_num and not beta_num:
            alpha = beta.new(beta.size()).fill_(alpha)
        elif not alpha_num and beta_num:
            beta = alpha.new(alpha.size()).fill_(beta)
        elif alpha_num and beta_num:
            alpha, beta = torch.Tensor([alpha]), torch.Tensor([beta])
        elif alpha.size() != beta.size():
            raise ValueError('Expected alpha.size() == beta.size(), actual {} vs {}'.format(
                alpha.size(), beta.size()))
        self.alpha = alpha
        self.beta = beta

    def sample(self):
        return _standard_gamma(self.alpha) / self.beta

    def sample_n(self, n):
        return _standard_gamma(_expand_n(self.alpha, n)) / self.beta

    def log_prob(self, value):
        return (self.alpha * torch.log(self.beta)
                + (self.alpha - 1) * torch.log(value)
                - self.beta * value - torch.lgamma(self.alpha))


def _dirichlet_sample_nograd(alpha):
    gammas = _standard_gamma(alpha)
    return gammas / gammas.sum(-1, True)


class _Dirichlet(Function):
    @staticmethod
    def forward(ctx, alpha):
        x = _dirichlet_sample_nograd(alpha)
        ctx.save_for_backward(x, alpha)
        return x

    @staticmethod
    @once_differentiable
    def backward(ctx, grad_output):
        x, alpha = ctx.saved_tensors
        total = alpha.sum(-1, True).expand_as(alpha)
        grad = torch._C._dirichlet_grad(x, alpha, total)
        return grad_output * grad


def _dirichlet_sample(alpha):
    if not isinstance(alpha, Variable):
        return _dirichlet_sample_nograd(alpha)
    if not alpha.requires_grad:
        return Variable(_dirichlet_sample_nograd(alpha.data))
    return _StandardGamma.apply(alpha)


class Dirichlet(Distribution):
    r"""
    Creates a Dirichlet distribution parameterized by concentration `alpha`.

    Example::

        >>> m = Dirichlet(torch.Tensor([0.5, 0.5]))
        >>> m.sample()  # Dirichlet distributed with concentrarion alpha
         0.1046
         0.8954
        [torch.FloatTensor of size 2]

    Args:
        alpha (Tensor or Variable): concentration parameter of the distribution
    """
    reparameterized = True

    def __init__(self, alpha):
        self.alpha = alpha

    def sample(self):
        return _dirichlet_sample(self.alpha)

    def sample_n(self, n):
        return _dirichlet_sample(_expand_n(self.alpha, n))

    def log_prob(self, value):
        return ((torch.log(value) * (self.alpha - 1.0)).sum(-1)
                + torch.lgamma(self.alpha.sum(-1))
                - torch.lgamma(self.alpha).sum(-1))


class Beta(Distribution):
    r"""
    Creates a Beta distribution parameterized by concentration `alpha` and `beta`.

    Example::

        >>> m = Beta(torch.Tensor([0.5]), torch.Tensor([0.5]))
        >>> m.sample()  # Beta distributed with concentrarion alpha
         0.1046
        [torch.FloatTensor of size 2]

    Args:
        alpha (Tensor or Variable): concentration parameter of the distribution
    """
    reparameterized = Dirichlet.reparameterized

    def __init__(self, alpha, beta):
        alpha_num = isinstance(alpha, Number)
        beta_num = isinstance(beta, Number)
        if alpha_num and beta_num:
            alpha_beta = torch.Tensor([alpha, beta])
        else:
            if alpha_num and not beta_num:
                alpha = beta.new(beta.size()).fill_(alpha)
            elif not alpha_num and beta_num:
                beta = alpha.new(alpha.size()).fill_(beta)
            elif alpha.size() != beta.size():
                raise ValueError('Expected alpha.size() == beta.size(), actual {} vs {}'.format(
                    alpha.size(), beta.size()))
            alpha_beta = torch.stack([alpha, beta], -1)
        self.dirichlet = Dirichlet(alpha_beta)

    def sample(self):
        return self.dirichlet.sample().select(-1, 0)

    def sample_n(self, n):
        return self.dirichlet.sample_n(n).select(-1, 0)

    def log_prob(self, value):
        if isinstance(value, Number):
            heads_tails = torch.Tensor([value, 1.0 - value])
        else:
            heads_tails = torch.stack([value, 1.0 - value])
        return self.dirichlet.log_prob(heads_tails)
