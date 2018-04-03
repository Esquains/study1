r"""
The torch package contains data structures for multi-dimensional
tensors and mathematical operations over these are defined.
Additionally, it provides many utilities for efficient serializing of
Tensors and arbitrary types, and other useful utilities.

It has a CUDA counterpart, that enables you to run your tensor computations
on an NVIDIA GPU with compute capability >= 3.0.
"""

import sys
import platform
from ._utils import _import_dotted_name
from .version import __version__
from ._six import string_classes as _string_classes

__all__ = [
    'typename', 'is_tensor', 'is_storage', 'set_default_tensor_type',
    'set_rng_state', 'get_rng_state', 'manual_seed', 'initial_seed',
    'save', 'load', 'set_printoptions', 'chunk', 'split', 'stack', 'matmul',
    'no_grad', 'enable_grad',
    'DoubleStorage', 'FloatStorage', 'LongStorage', 'IntStorage',
    'ShortStorage', 'CharStorage', 'ByteStorage',
    'DoubleTensor', 'FloatTensor', 'LongTensor', 'IntTensor',
    'ShortTensor', 'CharTensor', 'ByteTensor', 'Tensor',
]

################################################################################
# Load the extension module
################################################################################

# Loading the extension with RTLD_GLOBAL option allows to not link extension
# modules against the _C shared object. Their missing THP symbols will be
# automatically filled by the dynamic loader.
import os as _dl_flags

# if we have numpy, it *must* be imported before the call to setdlopenflags()
# or there is risk that later c modules will segfault when importing numpy
try:
    import numpy as _np
except ImportError:
    pass

if platform.system() == 'Windows':
    # first get nvToolsExt PATH
    def get_nvToolsExt_path():
        NVTOOLEXT_HOME = _dl_flags.getenv('NVTOOLSEXT_PATH', 'C:\\Program Files\\NVIDIA Corporation\\NvToolsExt')

        if _dl_flags.path.exists(NVTOOLEXT_HOME):
            return NVTOOLEXT_HOME + '\\bin\\x64\\'
        else:
            return ''

    # then add the path to env
    _dl_flags.environ['PATH'] = _dl_flags.path.dirname(
        __file__) + '\\lib\\;' + get_nvToolsExt_path() + ';' + _dl_flags.environ['PATH']

else:
    # first check if the os package has the required flags
    if not hasattr(_dl_flags, 'RTLD_GLOBAL') or not hasattr(_dl_flags, 'RTLD_LAZY'):
        try:
            # next try if DLFCN exists
            import DLFCN as _dl_flags
        except ImportError:
            # as a last attempt, use compile-time constants
            import torch._dl as _dl_flags

    old_flags = sys.getdlopenflags()
    sys.setdlopenflags(_dl_flags.RTLD_GLOBAL | _dl_flags.RTLD_LAZY)

del _dl_flags

try:
    import torch._nvrtc
except ImportError:
    pass

from torch._C import *

__all__ += [name for name in dir(_C)
            if name[0] != '_' and
            not name.endswith('Base')]

if platform.system() != 'Windows':
    sys.setdlopenflags(old_flags)
    del old_flags

################################################################################
# Define basic utilities
################################################################################


def typename(o):
    if isinstance(o, torch.Tensor):
        return o.type()

    module = ''
    class_name = ''
    if hasattr(o, '__module__') and o.__module__ != 'builtins' \
            and o.__module__ != '__builtin__' and o.__module__ is not None:
        module = o.__module__ + '.'

    if hasattr(o, '__qualname__'):
        class_name = o.__qualname__
    elif hasattr(o, '__name__'):
        class_name = o.__name__
    else:
        class_name = o.__class__.__name__

    return module + class_name


def is_tensor(obj):
    r"""Returns True if `obj` is a PyTorch tensor.

    Args:
        obj (Object): Object to test
    """
    return isinstance(obj, torch.Tensor)


def is_storage(obj):
    r"""Returns True if `obj` is a PyTorch storage object.

    Args:
        obj (Object): Object to test
    """
    return type(obj) in _storage_classes


def set_default_tensor_type(t):
    r"""Sets the default ``torch.Tensor`` type to type :attr:`t`.

    Args:
        t (type or string): the tensor type or its name

    Example::

        >>> torch.set_default_tensor_type("torch.FloatTensor")
        >>> torch.Tensor([1.2, 3])

         1.2000
         3.0000
        [torch.FloatTensor of size (2,)]

        >>> torch.set_default_tensor_type(torch.double)
        >>> torch.Tensor([1.2, 3])

         1.2000
         3.0000
        [torch.DoubleTensor of size (2,)]

    """
    if isinstance(t, _string_classes):
        t = _import_dotted_name(t)
    _C._set_default_tensor_type(t)

from .random import set_rng_state, get_rng_state, manual_seed, initial_seed
from .serialization import save, load
from ._tensor_str import set_printoptions

################################################################################
# Define Storage and Tensor classes
################################################################################

from .tensor import Tensor
from .storage import _StorageBase


class DoubleStorage(_C.DoubleStorageBase, _StorageBase):
    pass


class FloatStorage(_C.FloatStorageBase, _StorageBase):
    pass


class HalfStorage(_C.HalfStorageBase, _StorageBase):
    pass


class LongStorage(_C.LongStorageBase, _StorageBase):
    pass


class IntStorage(_C.IntStorageBase, _StorageBase):
    pass


class ShortStorage(_C.ShortStorageBase, _StorageBase):
    pass


class CharStorage(_C.CharStorageBase, _StorageBase):
    pass


class ByteStorage(_C.ByteStorageBase, _StorageBase):
    pass


_storage_classes = {
    DoubleStorage, FloatStorage, LongStorage, IntStorage, ShortStorage,
    CharStorage, ByteStorage, HalfStorage
}

# The _tensor_classes set is initialized by the call to _C._initialize_tensor_type_bindings()
_tensor_classes = set()


################################################################################
# Initialize extension
################################################################################

def manager_path():
    if platform.system() == 'Windows':
        return b""
    import os
    path = os.path.join(os.path.abspath(os.path.dirname(__file__)), 'lib', 'torch_shm_manager')
    if not os.path.exists(path):
        raise RuntimeError("Unable to find torch_shm_manager at " + path)
    return path.encode('utf-8')


# Shared memory manager needs to know the exact location of manager executable
_C._initExtension(manager_path())
del manager_path

for name in dir(_C._VariableFunctions):
    globals()[name] = getattr(_C._VariableFunctions, name)

################################################################################
# Import interface functions defined in Python
################################################################################

# needs to be after the above ATen bindings so we can overwrite from Python side
from .functional import *


################################################################################
# Remove unnecessary members
################################################################################

del DoubleStorageBase
del FloatStorageBase
del LongStorageBase
del IntStorageBase
del ShortStorageBase
del CharStorageBase
del ByteStorageBase

################################################################################
# Import most common subpackages
################################################################################

import torch.cuda
import torch.autograd
import torch.nn
import torch.optim
import torch.multiprocessing
import torch.sparse
import torch.utils.backcompat
import torch.onnx
import torch.jit
import torch.random
import torch.distributions
import torch.testing
from torch.autograd import no_grad, enable_grad, set_grad_enabled

_C._init_names(list(torch._storage_classes))

# attach docstrings to torch and tensor functions
from . import _torch_docs, _tensor_docs, _storage_docs
del _torch_docs, _tensor_docs, _storage_docs
