# This file was generated by jschema_to_python version 0.0.1.dev29.

from __future__ import annotations

import dataclasses
from typing import List, Optional

from torch.onnx._internal.diagnostics.infra.sarif_om import (
    _external_property_file_reference,
    _property_bag,
)


@dataclasses.dataclass
class ExternalPropertyFileReferences(object):
    """References to external property files that should be inlined with the content of a root log file."""

    addresses: Optional[
        List[_external_property_file_reference.ExternalPropertyFileReference]
    ] = dataclasses.field(default=None, metadata={"schema_property_name": "addresses"})
    artifacts: Optional[
        List[_external_property_file_reference.ExternalPropertyFileReference]
    ] = dataclasses.field(default=None, metadata={"schema_property_name": "artifacts"})
    conversion: Optional[
        _external_property_file_reference.ExternalPropertyFileReference
    ] = dataclasses.field(default=None, metadata={"schema_property_name": "conversion"})
    driver: Optional[
        _external_property_file_reference.ExternalPropertyFileReference
    ] = dataclasses.field(default=None, metadata={"schema_property_name": "driver"})
    extensions: Optional[
        List[_external_property_file_reference.ExternalPropertyFileReference]
    ] = dataclasses.field(default=None, metadata={"schema_property_name": "extensions"})
    externalized_properties: Optional[
        _external_property_file_reference.ExternalPropertyFileReference
    ] = dataclasses.field(
        default=None, metadata={"schema_property_name": "externalizedProperties"}
    )
    graphs: Optional[
        List[_external_property_file_reference.ExternalPropertyFileReference]
    ] = dataclasses.field(default=None, metadata={"schema_property_name": "graphs"})
    invocations: Optional[
        List[_external_property_file_reference.ExternalPropertyFileReference]
    ] = dataclasses.field(
        default=None, metadata={"schema_property_name": "invocations"}
    )
    logical_locations: Optional[
        List[_external_property_file_reference.ExternalPropertyFileReference]
    ] = dataclasses.field(
        default=None, metadata={"schema_property_name": "logicalLocations"}
    )
    policies: Optional[
        List[_external_property_file_reference.ExternalPropertyFileReference]
    ] = dataclasses.field(default=None, metadata={"schema_property_name": "policies"})
    properties: Optional[_property_bag.PropertyBag] = dataclasses.field(
        default=None, metadata={"schema_property_name": "properties"}
    )
    results: Optional[
        List[_external_property_file_reference.ExternalPropertyFileReference]
    ] = dataclasses.field(default=None, metadata={"schema_property_name": "results"})
    taxonomies: Optional[
        List[_external_property_file_reference.ExternalPropertyFileReference]
    ] = dataclasses.field(default=None, metadata={"schema_property_name": "taxonomies"})
    thread_flow_locations: Optional[
        List[_external_property_file_reference.ExternalPropertyFileReference]
    ] = dataclasses.field(
        default=None, metadata={"schema_property_name": "threadFlowLocations"}
    )
    translations: Optional[
        List[_external_property_file_reference.ExternalPropertyFileReference]
    ] = dataclasses.field(
        default=None, metadata={"schema_property_name": "translations"}
    )
    web_requests: Optional[
        List[_external_property_file_reference.ExternalPropertyFileReference]
    ] = dataclasses.field(
        default=None, metadata={"schema_property_name": "webRequests"}
    )
    web_responses: Optional[
        List[_external_property_file_reference.ExternalPropertyFileReference]
    ] = dataclasses.field(
        default=None, metadata={"schema_property_name": "webResponses"}
    )


# flake8: noqa
