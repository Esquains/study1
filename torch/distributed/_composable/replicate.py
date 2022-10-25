import torch
import torch.distributed as dist
import torch.nn as nn

from typing import List, Tuple, Optional


class DistributedState:
    ...


def _get_module_dist_state(module: nn.Module) -> Optional[DistributedState]:
    return getattr(module, "_distributed_state", None)


def _set_module_dist_state(module: nn.Module, state: DistributedState) -> None:
    module._distributed_state = state  # type: ignore[assignment]


class ReplicateState(DistributedState):
    def __init__(self) -> None:
        self.modules: List[nn.Module] = []
        self.parameters: List[nn.Parameter] = []
        self.has_initialized: bool = False

    def add_modules(self, *modules: nn.Module) -> None:
        for module in modules:
            self.modules.append(module)
            _set_module_dist_state(module, self)
            module.register_forward_pre_hook(self.forward_pre_hook)
            module.register_forward_hook(self.forward_post_hook)

    def _recursive_collect_params(self, module: nn.Module) -> None:
        if _get_module_dist_state(module) is not None:
            return
        self.parameters.extend(
            param for param in module.parameters() if param.requires_grad
        )
        for child in module.children():
            self._recursive_collect_params(child)

    def init_helper(self):
        self.has_initialized = True
        for module in self.modules:
            self._recursive_collect_params(module)
        # broadcast parameters, create Reducer
        ...

    def forward_pre_hook(
        self, module: nn.Module, input: Tuple[torch.Tensor]
    ) -> None:
        if not self.has_initialized:
            self.init_helper()
        ...

    def forward_post_hook(
        self,
        module: nn.Module,
        input: Tuple[torch.Tensor],
        output: torch.Tensor,
    ) -> None:
        ...


# TODO(@yhcharles): use a per-model instance instead of a global one
_default_state = ReplicateState()


def replicate(
    *modules: nn.Module, dist_state: ReplicateState = _default_state
) -> None:
    r"""Replicates module(s)
    Args:
        modules (torch.nn.Module): modules to replicate

    Example::
        >>> module = nn.Linear(3, 3)
        >>> replicate(module)
    """
    dist_state.add_modules(*modules)
