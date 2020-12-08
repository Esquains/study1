import torch
from ..optimizer import Optimizer, required


class SGD(Optimizer):
    r"""Implements stochastic gradient descent (optionally with momentum).

    Nesterov momentum is based on the formula from
    `On the importance of initialization and momentum in deep learning`__.

    Args:
        params (iterable): iterable of parameters to optimize or dicts defining
            parameter groups
        lr (float): learning rate
        momentum (float, optional): momentum factor (default: 0)
        weight_decay (float, optional): weight decay (L2 penalty) (default: 0)
        dampening (float, optional): dampening for momentum (default: 0)
        nesterov (bool, optional): enables Nesterov momentum (default: False)

    Example:
        >>> optimizer = torch.optim.SGD(model.parameters(), lr=0.1, momentum=0.9)
        >>> optimizer.zero_grad()
        >>> loss_fn(model(input), target).backward()
        >>> optimizer.step()

    __ http://www.cs.toronto.edu/%7Ehinton/absps/momentum.pdf

    .. note::
        The implementation of SGD with Momentum/Nesterov subtly differs from
        Sutskever et. al. and implementations in some other frameworks.

        Considering the specific case of Momentum, the update can be written as

        .. math::
            \begin{aligned}
                v_{t+1} & = \mu * v_{t} + g_{t+1}, \\
                p_{t+1} & = p_{t} - \text{lr} * v_{t+1},
            \end{aligned}

        where :math:`p`, :math:`g`, :math:`v` and :math:`\mu` denote the 
        parameters, gradient, velocity, and momentum respectively.

        This is in contrast to Sutskever et. al. and
        other frameworks which employ an update of the form

        .. math::
            \begin{aligned}
                v_{t+1} & = \mu * v_{t} + \text{lr} * g_{t+1}, \\
                p_{t+1} & = p_{t} - v_{t+1}.
            \end{aligned}

        The Nesterov version is analogously modified.
    """

    def __init__(self, params, lr=required, momentum=0, dampening=0,
                 weight_decay=0, nesterov=False):
        if lr is not required and lr < 0.0:
            raise ValueError("Invalid learning rate: {}".format(lr))
        if momentum < 0.0:
            raise ValueError("Invalid momentum value: {}".format(momentum))
        if weight_decay < 0.0:
            raise ValueError("Invalid weight_decay value: {}".format(weight_decay))

        defaults = dict(lr=lr, momentum=momentum, dampening=dampening,
                        weight_decay=weight_decay, nesterov=nesterov)
        if nesterov and (momentum <= 0 or dampening != 0):
            raise ValueError("Nesterov momentum requires a momentum and zero dampening")
        super(SGD, self).__init__(params, defaults)

    def __setstate__(self, state):
        super(SGD, self).__setstate__(state)
        for group in self.param_groups:
            group.setdefault('nesterov', False)

    @torch.no_grad()
    def step(self, closure=None):
        """Performs a single optimization step.

        Arguments:
            closure (callable, optional): A closure that reevaluates the model
                and returns the loss.
        """
        loss = None
        if closure is not None:
            with torch.enable_grad():
                loss = closure()

        for group in self.param_groups:
            weight_decay = group['weight_decay']
            momentum = group['momentum']
            dampening = group['dampening']
            nesterov = group['nesterov']

            # filter params into two groups, those that already have momemtum and those that dont
            # momentum_buf as mb
            grads_mb = []
            grads_no_mb = []
            params_with_grad_mb = []
            params_with_grad_no_mb = []
            has_sparse_grad = False
            bufs_mb = []
            bufs_no_mb = []

            for p in group['params']:
                if p.grad is not None:
                    curr_grad = p.grad

                    if 'momentum_buffer' in self.state[p]: 
                        grads_mb.append(curr_grad)
                        params_with_grad_mb.append(p)
                        bufs_mb.append(self.state[p]['momentum_buffer'])
                    else:
                        grads_no_mb.append(curr_grad)
                        params_with_grad_no_mb.append(p)

                    if p.grad.is_sparse:
                        has_sparse_grad = True

                        if momentum != 0: 
                            raise RuntimeError('SGD does not support momentum for sparse gradients')

            if weight_decay != 0 and grads_no_mb != []:
                grads_no_mb = torch._foreach_add(grads_no_mb, params_with_grad_no_mb, alpha=weight_decay)
                for p, curr_grad in zip(params_with_grad_no_mb, grads_no_mb):
                    self.state[p]['momentum_buffer'] = torch.clone(curr_grad).detach()
                    bufs_no_mb.append(self.state[p]['momentum_buffer'])

            if weight_decay != 0 and grads_mb != []:
                grads_mb = torch._foreach_add(grads_mb, params_with_grad_mb, alpha=weight_decay)

            if grads_mb == [] and grads_no_mb == []:
                return loss

            if momentum != 0 and grads_mb != []:
                torch._foreach_mul_(bufs_mb, momentum)
                torch._foreach_add_(bufs_mb, grads_mb, alpha=1 - dampening)

            def perform_step(grads, params_with_grad, bufs):
                if momentum != 0:
                    if nesterov:
                        grads = torch._foreach_add(grads, bufs, alpha=momentum)
                    else:
                        grads = bufs

                if not has_sparse_grad:
                    torch._foreach_add_(params_with_grad, grads, alpha=-group['lr'])
                else:
                    # foreach APIs dont support sparse
                    for i in range(len(params_with_grad)): 
                        params_with_grad[i].add_(grads[i], alpha=-group['lr'])

            if grads_mb != []:
                perform_step(grads_mb, params_with_grad_mb, bufs_mb)

            if grads_no_mb != []:
                perform_step(grads_no_mb, params_with_grad_no_mb, bufs_no_mb)

        return loss
