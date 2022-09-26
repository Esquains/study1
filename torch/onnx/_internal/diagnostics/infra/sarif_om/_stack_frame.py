# This file was generated by jschema_to_python version 0.0.1.dev29.

from __future__ import annotations

import dataclasses
from typing import List, Optional

from torch.onnx._internal.diagnostics.infra.sarif_om import _location, _property_bag


@dataclasses.dataclass
class StackFrame(object):
    """A function call within a stack trace."""

    location: Optional[_location.Location] = dataclasses.field(
        default=None, metadata={"schema_property_name": "location"}
    )
    module: Optional[str] = dataclasses.field(
        default=None, metadata={"schema_property_name": "module"}
    )
    parameters: Optional[List[str]] = dataclasses.field(
        default=None, metadata={"schema_property_name": "parameters"}
    )
    properties: Optional[_property_bag.PropertyBag] = dataclasses.field(
        default=None, metadata={"schema_property_name": "properties"}
    )
    thread_id: Optional[int] = dataclasses.field(
        default=None, metadata={"schema_property_name": "threadId"}
    )


# flake8: noqa
