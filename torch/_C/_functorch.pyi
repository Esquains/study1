# mypy: allow-untyped-defs
from enum import Enum

from torch import Tensor

# Defined in torch/csrc/functorch/init.cpp

def _set_dynamic_layer_keys_included(included: bool) -> None: ...
def get_unwrapped(tensor: Tensor) -> Tensor: ...
def is_batchedtensor(tensor: Tensor) -> bool: ...
def is_functionaltensor(tensor: Tensor) -> bool: ...
def is_functorch_wrapped_tensor(tensor: Tensor) -> bool: ...
def is_gradtrackingtensor(tensor: Tensor) -> bool: ...
def is_legacy_batchedtensor(tensor: Tensor) -> bool: ...
def maybe_get_bdim(tensor: Tensor) -> int: ...
def maybe_get_level(tensor: Tensor) -> int: ...
def maybe_current_level() -> int | None: ...
def unwrap_if_dead(tensor: Tensor) -> Tensor: ...
def _unwrap_for_grad(tensor: Tensor, level: int) -> Tensor: ...
def _wrap_for_grad(tensor: Tensor, level: int) -> Tensor: ...
def _unwrap_batched(tensor: Tensor, level: int) -> tuple[Tensor, int | None]: ...
def current_level() -> int: ...
def count_jvp_interpreters() -> int: ...
def _add_batch_dim(tensor: Tensor, bdim: int, level: int) -> Tensor: ...
def set_single_level_autograd_function_allowed(allowed: bool) -> None: ...
def get_single_level_autograd_function_allowed() -> bool: ...
def _unwrap_functional_tensor(tensor: Tensor, reapply_views: bool) -> Tensor: ...
def _wrap_functional_tensor(tensor: Tensor, level: int) -> Tensor: ...
def _vmap_increment_nesting(batch_size: int, randomness: str) -> int: ...
def _vmap_decrement_nesting() -> int: ...
def _grad_increment_nesting() -> int: ...
def _grad_decrement_nesting() -> int: ...
def _jvp_increment_nesting() -> int: ...
def _jvp_decrement_nesting() -> int: ...

# Defined in aten/src/ATen/functorch/Interpreter.h
class TransformType(Enum):
    Torch: TransformType = ...
    Vmap: TransformType = ...
    Grad: TransformType = ...
    Jvp: TransformType = ...
    Functionalize: TransformType = ...

class RandomnessType(Enum):
    Error: TransformType = ...
    Same: TransformType = ...
    Different: TransformType = ...

class CInterpreter:
    def key(self) -> TransformType: ...
    def level(self) -> int: ...

class CGradInterpreterPtr:
    def __init__(self, interpreter: CInterpreter) -> None: ...
    def lift(self, Tensor) -> Tensor: ...
    def prevGradMode(self) -> bool: ...

class CJvpInterpreterPtr:
    def __init__(self, interpreter: CInterpreter) -> None: ...
    def lift(self, Tensor) -> Tensor: ...
    def prevFwdGradMode(self) -> bool: ...

class CFunctionalizeInterpreterPtr:
    def __init__(self, interpreter: CInterpreter) -> None: ...
    def key(self) -> TransformType: ...
    def level(self) -> int: ...
    def functionalizeAddBackViews(self) -> bool: ...

class CVmapInterpreterPtr:
    def __init__(self, interpreter: CInterpreter) -> None: ...
    def key(self) -> TransformType: ...
    def level(self) -> int: ...
    def batchSize(self) -> int: ...
    def randomness(self) -> RandomnessType: ...

class DynamicLayer: ...

def get_dynamic_layer_stack_depth() -> int: ...
def get_interpreter_stack() -> list[CInterpreter]: ...
def peek_interpreter_stack() -> CInterpreter: ...
def pop_dynamic_layer_stack() -> DynamicLayer: ...
def pop_dynamic_layer_stack_and_undo_to_depth(int) -> None: ...
def push_dynamic_layer_stack(dl: DynamicLayer) -> int: ...
