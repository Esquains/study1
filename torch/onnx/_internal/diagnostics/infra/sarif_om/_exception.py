# This file was generated by jschema_to_python version 0.0.1.dev29.

from __future__ import annotations

import dataclasses
from typing import List, Optional

from torch.onnx._internal.diagnostics.infra.sarif_om import (
    _exception,
    _property_bag,
    _stack,
)


@dataclasses.dataclass
class Exception(object):
    """Describes a runtime exception encountered during the execution of an analysis tool."""

    inner_exceptions: Optional[List[_exception.Exception]] = dataclasses.field(
        default=None, metadata={"schema_property_name": "innerExceptions"}
    )
    kind: Optional[str] = dataclasses.field(
        default=None, metadata={"schema_property_name": "kind"}
    )
    message: Optional[str] = dataclasses.field(
        default=None, metadata={"schema_property_name": "message"}
    )
    properties: Optional[_property_bag.PropertyBag] = dataclasses.field(
        default=None, metadata={"schema_property_name": "properties"}
    )
    stack: Optional[_stack.Stack] = dataclasses.field(
        default=None, metadata={"schema_property_name": "stack"}
    )


# flake8: noqa
