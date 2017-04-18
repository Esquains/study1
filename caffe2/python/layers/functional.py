## @package functional
# Module caffe2.python.layers.functional
from __future__ import absolute_import
from __future__ import division
from __future__ import print_function
from __future__ import unicode_literals

from caffe2.python import core, schema, scope, workspace
from caffe2.python.layers.layers import (
    ModelLayer,
)
import caffe2.proto.caffe2_pb2 as caffe2_pb2
import numpy as np
import logging

logger = logging.getLogger(__name__)
logger.setLevel(logging.INFO)


class Functional(ModelLayer):

    def __init__(self, model, input_record, output_names_or_num, function,
                 name='functional', output_dtypes=None, **kwargs):

        # allow coercion
        input_record = schema.as_record(input_record)

        super(Functional, self).__init__(model, name, input_record, **kwargs)
        self._function = function

        with scope.NameScope(self.name):
            if isinstance(output_names_or_num, int):
                self.output_schema = schema.NewRecord(
                    model.net, schema.RawTuple(output_names_or_num))
            else:
                if not isinstance(output_names_or_num, list):
                    output_names_or_num = [output_names_or_num]
                out_tuple = [(out, np.void) for out in output_names_or_num]
                self.output_schema = schema.NewRecord(
                    model.net, schema.Struct(*out_tuple))

        num_outputs = len(self.output_schema.field_blobs())

        # If output_dtypes is provided, use it for output schema. Otherwise
        # the shape and type will be inferred.
        if output_dtypes is not None:
            if not isinstance(output_dtypes, list):
                output_dtypes = [output_dtypes] * num_outputs
            assert len(output_dtypes) == num_outputs
            for dtype, scalar in zip(output_dtypes,
                                     self.output_schema.all_scalars()):
                scalar.set_type(dtype)
            return

        # Fake execution of the function to infer shapes and types automatically
        had_issues = False
        try:
            type_net = core.Net('_temp_type_and_shape_inference_net')
            schema.InitEmptyRecord(type_net, input_record, enforce_types=True)

            function(type_net, self.input_record, self.output_schema)
            (shapes, types) = workspace.InferShapesAndTypes([type_net], {})
            for i in range(num_outputs):
                blob = self.output_schema[i]()
                if blob not in types or blob not in shapes:
                    had_issues = True
                    continue
                if shapes[blob] == []:
                    # Scalar type
                    shape = tuple()
                elif shapes[blob][0] == 0:
                    shape = tuple(shapes[blob][1:])
                else:
                    logger.warning("unexpeced shape: {}".format(shapes[blob]))
                    # If batch dimension is not first - give up on shape
                    # inference for that blob
                    had_issues = True
                    continue

                # TODO(amalevich): Move it to some shared library
                dtype = None
                if types[blob] == caffe2_pb2.TensorProto.DOUBLE:
                    dtype = (np.float64, shape)
                elif types[blob] == caffe2_pb2.TensorProto.FLOAT:
                    dtype = (np.float32, shape)
                elif types[blob] == caffe2_pb2.TensorProto.INT32:
                    dtype = (np.int32, shape)
                elif types[blob] == caffe2_pb2.TensorProto.INT64:
                    dtype = (np.int64, shape)

                if dtype is not None:
                    self.output_schema[i].set_type(dtype)
        except TypeError as ex:
            had_issues = True
            logger.warning(str(ex))

        if had_issues:
            logger.warning(
                "Type inference had problems for layer: {}".format(self.name))

    def add_ops(self, net):
        self._function(net, self.input_record, self.output_schema)
