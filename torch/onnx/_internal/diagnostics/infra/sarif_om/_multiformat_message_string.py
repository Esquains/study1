# This file was generated by jschema_to_python version 0.0.1.dev29.

from __future__ import annotations

import dataclasses
from typing import Optional

from torch.onnx._internal.diagnostics.infra.sarif_om import _property_bag


@dataclasses.dataclass
class MultiformatMessageString(object):
    """A message string or message format string rendered in multiple formats."""

    text: str = dataclasses.field(metadata={"schema_property_name": "text"})
    markdown: Optional[str] = dataclasses.field(
        default=None, metadata={"schema_property_name": "markdown"}
    )
    properties: Optional[_property_bag.PropertyBag] = dataclasses.field(
        default=None, metadata={"schema_property_name": "properties"}
    )


# flake8: noqa
