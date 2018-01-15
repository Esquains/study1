from .module import Module
from .. import functional as F


class LocalResponseNorm(Module):
    def __init__(self, size, alpha=1e-4, beta=0.75, k=1):
        r"""Applies local response normalization to a 3D or higher dimensional
        signal, where channels occupy the second dimension:

        .. math::

            `b_{c} = a_{c}\left(k + \alpha\sum_{c'=\max(0, c-n/2)}^{\min(N-1,c+n/2)}
            a_{c'}^2\right)^{-\beta}`

        Args:
            size: amount of neighbouring channels used for normalization
            alpha: multiplicative factor
            beta: exponenent
            k: additive factor

        Shape:
            - Input: :math:`(N, C, ...)`
            - Output: :math:`(N, C, ...)` (same shape as input)
        Examples::
            >>> lrn = nn.LocalResponseNorm(2)
            >>> 2d_signal = autograd.Variable(torch.randn(32, 5, 24, 24))
            >>> 4d_signal = autograd.Variable(torch.randn(16, 5, 7, 7, 7, 7))
            >>> 2d_output = lrn(2d_signal)
            >>> 4d_output = lrn(4d_signal)
        """
        super(LocalResponseNorm, self).__init__()
        self.size = size
        self.alpha = alpha
        self.beta = beta
        self.k = k

    def forward(self, input):
        return F.local_response_norm(input, self.size, self.alpha, self.beta,
                                     self.k)

    def __repr__(self):
        return self.__class__.__name__ + '(' \
            + str(self.size) \
            + ', alpha=' + str(self.alpha) \
            + ', beta=' + str(self.beta) \
            + ', k=' + str(self.k) + ')'


class CrossMapLRN2d(Module):

    def __init__(self, size, alpha=1e-4, beta=0.75, k=1):
        super(CrossMapLRN2d, self).__init__()
        self.size = size
        self.alpha = alpha
        self.beta = beta
        self.k = k

    def forward(self, input):
        return self._backend.CrossMapLRN2d(self.size, self.alpha, self.beta,
                                           self.k)(input)

    def __repr__(self):
        return self.__class__.__name__ + '(' \
            + str(self.size) \
            + ', alpha=' + str(self.alpha) \
            + ', beta=' + str(self.beta) \
            + ', k=' + str(self.k) + ')'


# TODO: ContrastiveNorm2d
# TODO: DivisiveNorm2d
# TODO: SubtractiveNorm2d
