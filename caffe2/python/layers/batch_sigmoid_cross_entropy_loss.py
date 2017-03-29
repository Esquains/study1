## @package batch_sigmoid_cross_entropy_loss
# Module caffe2.python.layers.batch_sigmoid_cross_entropy_loss
from __future__ import absolute_import
from __future__ import division
from __future__ import print_function
from __future__ import unicode_literals

from caffe2.python import schema
from caffe2.python.layers.layers import ModelLayer
from caffe2.python.layers.tags import Tags
import numpy as np


class BatchSigmoidCrossEntropyLoss(ModelLayer):
    def __init__(
        self,
        model,
        input_record,
        name='batch_sigmoid_cross_entropy_loss',
        **kwargs
    ):
        super(BatchSigmoidCrossEntropyLoss, self).__init__(
            model, name, input_record, **kwargs)

        assert schema.is_schema_subset(
            schema.Struct(
                ('label', schema.Scalar(np.float32)),
                ('prediction', schema.Scalar(np.float32)),
            ),
            input_record
        )
        assert input_record.prediction.field_type().shape == \
            input_record.label.field_type().shape, \
            "prediction and label must have the same shape"

        self.tags.update({Tags.TRAIN_ONLY})

        self.output_schema = schema.Scalar(
            (np.float32, tuple()), model.net.NextScopedBlob(name + '_loss')
        )

    def add_ops(self, net):
        sigmoid_cross_entropy = net.SigmoidCrossEntropyWithLogits(
            [self.input_record.prediction(), self.input_record.label()],
            net.NextScopedBlob('sigmoid_cross_entropy')
        )

        net.AveragedLoss(
            sigmoid_cross_entropy, self.output_schema.field_blobs())
