import os
import inspect
import typing
import pathlib
import torch
from typing import Optional, Iterable, List, Dict
from collections import defaultdict
from types import CodeType

_IS_MONKEYTYPE_INSTALLED = True
try:
    import monkeytype
    from monkeytype import trace as monkeytype_trace
    from monkeytype.db.base import CallTraceThunk, CallTraceStore, CallTraceStoreLogger
    from monkeytype.config import _startswith, LIB_PATHS
    from monkeytype.tracing import CallTrace, CodeFilter
except ImportError:
    _IS_MONKEYTYPE_INSTALLED = False

def get_qualified_name(func):
    return func.__qualname__

if _IS_MONKEYTYPE_INSTALLED:

    class JitTypeTraceStoreLogger(CallTraceStoreLogger):
        """A JitTypeCallTraceLogger that stores logged traces in a CallTraceStore."""
        def __init__(self, store: CallTraceStore):
            super().__init__(store)

        def log(self, trace: CallTrace) -> None:
            self.traces.append(trace)

    class JitTypeTraceStore(CallTraceStore):
        def __init__(self):
            super().__init__()
            # A dictionary keeping all collected CallTrace
            # key is fully qualified name of called function
            # value is list of all CallTrace
            self.trace_records: Dict[str, list] = defaultdict(list)

        def add(self, traces: Iterable[CallTrace]):
            for t in traces:
                qualified_name = get_qualified_name(t.func)
                self.trace_records[qualified_name].append(t)

        def filter(
            self,
            qualified_name: str,
            qualname_prefix: Optional[str] = None,
            limit: int = 2000
        ) -> List[CallTraceThunk]:
            return self.trace_records[qualified_name]

        def analyze(self, qualified_name: str) -> Dict:
            # Analyze the types for the given module
            # and create a dictionary of all the types
            # for arguments.
            records = self.trace_records[qualified_name]
            all_args: dict = defaultdict(set)
            for record in records:
                for arg, arg_type in record.arg_types.items():
                    all_args[arg].add(arg_type)
            return all_args

        def consolidate_types(self, qualified_name: str) -> Dict:
            all_args = self.analyze(qualified_name)
            # If there are more types for an argument,
            # then consolidate the type to `Any` and replace the entry
            # by type `Any`.
            for arg, types in all_args.items():
                _all_type = " "
                for _type in types:
                    # If the type is a type imported from typing
                    # like Tuple, List, Dict then replace "typing."
                    # with a null string.
                    if inspect.getmodule(_type) == typing:
                        _type_to_string = str(_type)
                        _all_type += _type_to_string.replace('typing.', '') + ','
                    elif _type is torch.nn.parameter.Parameter:
                        # Check if the type is torch.nn.parameter.Parameter,
                        # use the entire quaalified name `torch.nn.parameter.Parameter`
                        # for type
                        _all_type += 'torch.nn.parameter.Parameter' + ','
                    else:
                        _all_type += _type.__name__ + ','
                _all_type = _all_type.lstrip(" ")  # Remove any trailing spaces

                if len(types) > 1:
                    all_args[arg] = {'Any'}
                else:
                    all_args[arg] = {_all_type[:-1]}
            return all_args

        def get_args_types(self, qualified_name: str) -> Dict:
            return self.consolidate_types(qualified_name)

    class JitTypeTraceConfig(monkeytype.config.Config):
        def __init__(self, s: JitTypeTraceStore):
            super().__init__()
            self.s = s

        def trace_logger(self) -> JitTypeTraceStoreLogger:
            """
            Returns a JitCallTraceStoreLogger that logs to the configured
            trace store.
            """
            return JitTypeTraceStoreLogger(self.trace_store())

        def trace_store(self) -> CallTraceStore:
            return self.s

        def code_filter(self) -> Optional[CodeFilter]:
            return jit_code_filter
else:
    # When MonkeyType is not installed, we provide dummy class definitions
    # for the below classes.
    class JitTypeTraceStoreLogger:  # type:  ignore[no-redef]
        def __init__(self):
            pass

    class JitTypeTraceStore:  # type:  ignore[no-redef]
        def __init__(self):
            self.trace_records = None

    class JitTypeTraceConfig:  # type:  ignore[no-redef]
        def __init__(self):
            pass

    monkeytype_trace = None  # type:  ignore[assignment]  # noqa: F811

def jit_code_filter(code: CodeType) -> bool:
    """
    Custom CodeFilter for Torchscript to trace forward calls
    The code filter is similar to default code filter for monkeytype and
    excludes tracing of stdlib and site-packages.
    """
    # Filter code without a source file
    if code.co_name != 'forward' and (not code.co_filename or code.co_filename[0] == '<'):
        return False

    filename = pathlib.Path(code.co_filename).resolve()
    # if MONKEYTYPE_TRACE_MODULES is defined, trace only specified packages or modules
    trace_modules_str = os.environ.get('MONKEYTYPE_TRACE_MODULES')
    if trace_modules_str is not None:
        trace_modules = trace_modules_str.split(',')
        # try to remove lib_path to only check package and module names
        for lib_path in LIB_PATHS:
            try:
                filename = filename.relative_to(lib_path)
                break
            except ValueError:
                pass
        return any(m == filename.stem or m in filename.parts for m in trace_modules)
    else:
        return not any(_startswith(filename, lib_path) for lib_path in LIB_PATHS)
