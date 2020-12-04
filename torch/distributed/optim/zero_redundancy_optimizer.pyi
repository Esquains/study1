from typing import Tuple, Optional, Any, List, Dict
from .optimizer import _params_t, Optimizer
from torch.nn import Parameter
import torch


class ZeroRedundancyOptimizer(Optimizer):
    def __init__(
        self,
        params: _params_t,
        optim: Optimizer = ...,
        group: Optional[Any] = ...,
        broadcast_buffer_size: int = ...,
        **default: Any
    ) -> None:
        ...

    def partition_parameters(self) -> List[List[dict]]:
        ...

    def per_device_params(self) -> Dict[torch.device, List[List[Parameter]]]:
        ...

    def param_to_rank(self) -> Dict[torch.Tensor, int]:
        ...

    def local_state_dict(self) -> Dict[Any, Any]:
        ...

    def consolidate_state_dict(self, recipient_rank: int = 0) -> None:
        ...

    def load_local_state_dict(self, state_dict: Dict[Any, Any]) -> None:
        ...

    def clip_grad_norm(self, max_norm: Union[float, int], norm_type: Union[float, int] = 2.0) -> torch.Tensor:
        ...
