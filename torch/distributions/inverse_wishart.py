import math
import warnings
from numbers import Number
from typing import Union

import torch
from torch.distributions import constraints
from torch.distributions.exp_family import ExponentialFamily
from torch.distributions.utils import lazy_property
from torch.distributions.multivariate_normal import _precision_to_scale_tril


_log_2 = math.log(2)


def _mvdigamma(x: torch.Tensor, p: int) -> torch.Tensor:
    assert x.gt((p - 1) / 2).all(), "Wrong domain for multivariate digamma function."
    return torch.digamma(
        x.unsqueeze(-1)
        - torch.arange(p, dtype=x.dtype, device=x.device).div(2).expand(x.shape + (-1,))
    ).sum(-1)

class InverseWishart(ExponentialFamily):
    r"""
    Creates a Inverse Wishart distribution parameterized by a symmetric positive definite matrix :math:`\Sigma`,
    or its Cholesky decomposition :math:`\mathbf{\Sigma} = \mathbf{L}\mathbf{L}^\top`

    Example:
        >>> m = InverseWishart(torch.eye(2), torch.Tensor([2]))
        >>> m.sample()  # Inverse Wishart distributed with mean=`df * I` and
                        # variance(x_ij)=`df` for i != j and variance(x_ij)=`2 * df` for i == j
    Args:
        covariance_matrix (Tensor): positive-definite covariance matrix
        precision_matrix (Tensor): positive-definite precision matrix
        scale_tril (Tensor): lower-triangular factor of covariance, with positive-valued diagonal
        df (float or Tensor): real-valued parameter larger than the (dimension of Square matrix) - 1
    Note:
        Only one of :attr:`covariance_matrix` or :attr:`precision_matrix` or
        :attr:`scale_tril` can be specified.
        Using :attr:`scale_tril` will be more efficient: all computations internally
        are based on :attr:`scale_tril`. If :attr:`covariance_matrix` or
        :attr:`precision_matrix` is passed instead, it is only used to compute
        the corresponding lower triangular matrices using a Cholesky decomposition.
        Inverse Wishart distirbution is inverse transformed distribution of `torch.distributions.wishart.Wishart`
        wishart and inverse wishart distributions may exhibit other advanctages according to the use case.
        :math:`\mathbf{\Sigma}` follows an inverse Wishart distribution with df :math:`\nu` and covariance matrix :math:`\Psi`
        when :math:`\mathbf{\Sigma}^{-1}` follows an Wishart distribution with df :math:`\nu + p - 1` and
        covariance matrix :math:`\Psi^{-1}`
    """
    arg_constraints = {
        'covariance_matrix': constraints.positive_definite,
        'precision_matrix': constraints.positive_definite,
        'scale_tril': constraints.lower_cholesky,
        'df': constraints.greater_than(0),
    }
    support = constraints.positive_definite
    has_rsample = True
    _mean_carrier_measure = 0

    def __init__(self,
                 df: Union[torch.Tensor, Number],
                 covariance_matrix: torch.Tensor = None,
                 precision_matrix: torch.Tensor = None,
                 scale_tril: torch.Tensor = None,
                 validate_args=None):
        assert (covariance_matrix is not None) + (scale_tril is not None) + (precision_matrix is not None) == 1, \
            "Exactly one of covariance_matrix or precision_matrix or scale_tril may be specified."

        param = next(p for p in (covariance_matrix, precision_matrix, scale_tril) if p is not None)

        if param.dim() < 2:
            raise ValueError("scale_tril must be at least two-dimensional, with optional leading batch dimensions")

        if isinstance(df, Number):
            batch_shape = torch.Size(param.shape[:-2])
            self.df = torch.tensor(df, dtype=param.dtype, device=param.device)
        else:
            batch_shape = torch.broadcast_shapes(param.shape[:-2], df.shape)
            self.df = df.expand(batch_shape)
        event_shape = param.shape[-2:]

        if self.df.le(event_shape[-1] - 1).any():
            raise ValueError(f"Value of df={df} expected to be greater than ndim={event_shape[-1]-1}.")

        if scale_tril is not None:
            self.scale_tril = param.expand(batch_shape + (-1, -1))
        elif covariance_matrix is not None:
            self.covariance_matrix = param.expand(batch_shape + (-1, -1))
        elif precision_matrix is not None:
            self.precision_matrix = param.expand(batch_shape + (-1, -1))

        self.arg_constraints['df'] = constraints.greater_than(event_shape[-1] - 1)
        if self.df.lt(event_shape[-1]).any():
            warnings.warn("Low df values detected. Singular samples are highly likely to occur for ndim - 1 < df < ndim.")

        super(InverseWishart, self).__init__(batch_shape, event_shape, validate_args=validate_args)
        self._batch_dims = [-(x + 1) for x in range(len(self._batch_shape))]

        if scale_tril is not None:
            self._unbroadcasted_scale_tril = scale_tril
        elif covariance_matrix is not None:
            self._unbroadcasted_scale_tril = torch.linalg.cholesky(covariance_matrix)
        else:  # precision_matrix is not None
            self._unbroadcasted_scale_tril = _precision_to_scale_tril(precision_matrix)

        # Chi2 distribution is needed for Bartlett decomposition sampling
        self._dist_chi2 = torch.distributions.chi2.Chi2(
            df=(
                self.df.unsqueeze(-1)
                - torch.arange(
                    self._event_shape[-1],
                    dtype=self._unbroadcasted_scale_tril.dtype,
                    device=self._unbroadcasted_scale_tril.device,
                ).expand(batch_shape + (-1,))
            )
        )

    def expand(self, batch_shape, _instance=None):
        new = self._get_checked_instance(InverseWishart, _instance)
        batch_shape = torch.Size(batch_shape)
        cov_shape = batch_shape + self.event_shape
        new._unbroadcasted_scale_tril = self._unbroadcasted_scale_tril.expand(cov_shape)
        new.df = self.df.expand(batch_shape)

        new._batch_dims = [-(x + 1) for x in range(len(batch_shape))]

        if 'covariance_matrix' in self.__dict__:
            new.covariance_matrix = self.covariance_matrix.expand(cov_shape)
        if 'scale_tril' in self.__dict__:
            new.scale_tril = self.scale_tril.expand(cov_shape)
        if 'precision_matrix' in self.__dict__:
            new.precision_matrix = self.precision_matrix.expand(cov_shape)

        # Chi2 distribution is needed for Bartlett decomposition sampling
        new._dist_chi2 = torch.distributions.chi2.Chi2(
            df=(
                new.df.unsqueeze(-1)
                - torch.arange(
                    self.event_shape[-1],
                    dtype=new._unbroadcasted_scale_tril.dtype,
                    device=new._unbroadcasted_scale_tril.device,
                ).expand(batch_shape + (-1,))
            )
        )

        super(InverseWishart, new).__init__(batch_shape, self.event_shape, validate_args=False)
        new._validate_args = self._validate_args
        return new

    @lazy_property
    def scale_tril(self):
        return self._unbroadcasted_scale_tril.expand(
            self._batch_shape + self._event_shape)

    @lazy_property
    def covariance_matrix(self):
        return (
            self._unbroadcasted_scale_tril @ self._unbroadcasted_scale_tril.transpose(-2, -1)
        ).expand(self._batch_shape + self._event_shape)

    @lazy_property
    def precision_matrix(self):
        identity = torch.eye(
            self._event_shape[-1],
            device=self._unbroadcasted_scale_tril.device,
            dtype=self._unbroadcasted_scale_tril.dtype,
        )
        return torch.cholesky_solve(
            identity, self._unbroadcasted_scale_tril
        ).expand(self._batch_shape + self._event_shape)

    @property
    def mean(self):
        nu = self.df  # has shape (batch_shape)
        p = self._event_shape[-1]  # has singleton shape
        if self.df.le(p + 1).any():
            raise ValueError("Mean of the Inverse Wishart distribution can be caculated only for df > ndim + 1.")
        return self.covariance_matrix / (nu - p - 1).clamp(min=1e-10).view(self._batch_shape + (1, 1))

    @property
    def variance(self):
        nu = self.df  # has shape (batch_shape)
        p = self._event_shape[-1]  # has singleton shape
        if self.df.le(p + 3).any():
            raise ValueError(
                "Elementwise variance of the Inverse Wishart distribution can be caculated only for df > ndim + 3."
            )
        if self.df.ge(p - 3).any():
            warnings.warn(
                """
                In singular case, Elementwise variance of the Inverse Wishart distribution
                can be caculated only for ndim > df + 3.
                """
            )
        V = self.covariance_matrix  # has shape (batch_shape x event_shape)
        diag_V = V.diagonal(dim1=-2, dim2=-1)
        eff_df = (nu - p).view(self._batch_shape + (1, 1))

        return (
            (eff_df + 1) * V.pow(2)
            + (eff_df - 1) * torch.einsum("...i,...j->...ij", diag_V, diag_V)
        ) / (eff_df * (eff_df - 1).pow(2) * (eff_df - 3)).clamp(min=1e-10)

    def _bartlett_sampling(self, sample_shape=torch.Size()):
        p = self._event_shape[-1]  # has singleton shape

        # Implemented Sampling using Bartlett decomposition
        noise = self._dist_chi2.rsample(sample_shape).sqrt().reciprocal().diag_embed(dim1=-2, dim2=-1)
        i, j = torch.tril_indices(p, p, offset=-1)
        noise[..., i, j] = torch.randn(
            torch.Size(sample_shape) + self._batch_shape + (int(0.5 * p * (p - 1)),),
            dtype=noise.dtype,
            device=noise.device,
        )
        chol = self._unbroadcasted_scale_tril @ noise
        return chol @ chol.transpose(-2, -1)

    def rsample(self, sample_shape=torch.Size(), max_try_correction=None):
        r"""
        .. warning::
            In some cases, sampling algorithn based on Bartlett decomposition may return singular matrix samples.
            Several tries to correct singular samples are performed by default, but it may end up returning
            singular matrix samples. Sigular samples may return `-inf` values in `.log_prob()`.
            In those cases, the user should validate the samples and either fix the value of `df`
            or adjust `max_try_correction` value for argument in `.rsample` accordingly.
        """

        if max_try_correction is None:
            max_try_correction = 3 if torch._C._get_tracing_state() else 10

        sample_shape = torch.Size(sample_shape)
        sample = self._bartlett_sampling(sample_shape)

        # Below part is to improve numerical stability temporally and should be removed in the future
        is_singular = self.support.check(sample)
        if self._batch_shape:
            is_singular = is_singular.amax(self._batch_dims)

        if torch._C._get_tracing_state():
            # Less optimized version for JIT
            for _ in range(max_try_correction):
                sample_new = self._bartlett_sampling(sample_shape)
                sample = torch.where(is_singular, sample_new, sample)

                is_singular = ~self.support.check(sample)
                if self._batch_shape:
                    is_singular = is_singular.amax(self._batch_dims)

        else:
            # More optimized version with data-dependent control flow.
            if is_singular.any():
                warnings.warn("Singular sample detected.")

                for _ in range(max_try_correction):
                    sample_new = self._bartlett_sampling(is_singular[is_singular].shape)
                    sample[is_singular] = sample_new

                    is_singular_new = ~self.support.check(sample_new)
                    if self._batch_shape:
                        is_singular_new = is_singular_new.amax(self._batch_dims)
                    is_singular[is_singular.clone()] = is_singular_new

                    if not is_singular.any():
                        break

        return sample

    def log_prob(self, value):
        if self._validate_args:
            self._validate_sample(value)
        nu = self.df  # has shape (batch_shape)
        p = self._event_shape[-1]  # has singleton shape
        return (
            - 0.5 * nu * p * _log_2
            - nu * self._unbroadcasted_scale_tril.diagonal(dim1=-2, dim2=-1).log().sum(-1)
            - torch.mvlgamma(0.5 * nu, p=p)
            - 0.5 * (nu + p + 1) * torch.linalg.slogdet(value).logabsdet
            - 0.5 * torch.cholesky_solve(value, self._unbroadcasted_scale_tril).diagonal(dim1=-2, dim2=-1).reciprocal().sum(dim=-1)
        )

    def entropy(self):
        nu = self.df  # has shape (batch_shape)
        p = self._event_shape[-1]  # has singleton shape
        V = self.covariance_matrix  # has shape (batch_shape x event_shape)
        return (
            (p + 1) * self._unbroadcasted_scale_tril.diagonal(dim1=-2, dim2=-1).log().sum(-1)
            - 0.5 * p * (p + 1) * _log_2
            + torch.mvlgamma(0.5 * nu, p=p)
            - 0.5 * (nu + p + 1) * _mvdigamma(0.5 * nu, p=p)
            + 0.5 * nu * p
        )

    @property
    def _natural_params(self):
        return (
            0.5 * self.df,
            - 0.5 * self.covariance_matrix
        )

    def _log_normalizer(self, x, y):
        p = y.shape[-1]
        return x * (torch.linalg.slogdet(-2 * y).logabsdet + _log_2 * p) + _mvdigamma(x, p=p)
