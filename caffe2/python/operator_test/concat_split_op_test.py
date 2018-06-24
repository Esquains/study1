from __future__ import unicode_literals
from __future__ import absolute_import
from __future__ import division
from __future__ import print_function

import numpy as np
import hypothesis.strategies as st
import unittest
import caffe2.python.hypothesis_test_util as hu
from caffe2.proto import caffe2_pb2
from caffe2.python import core
from hypothesis import given


@st.composite
def _tensor_splits(draw, add_axis=False):
    """Generates (axis, split_info, tensor_splits) tuples."""
    tensor = draw(hu.tensor(min_value=4))  # Each dim has at least 4 elements.
    axis = draw(st.integers(-len(tensor.shape), len(tensor.shape) - 1))
    if add_axis:
        # Simple case: get individual slices along one axis, where each of them
        # is (N-1)-dimensional. The axis will be added back upon concatenation.
        return (
            axis,
            np.ones(tensor.shape[axis], dtype=np.int32),
            [
                np.array(tensor.take(i, axis=axis))
                for i in range(tensor.shape[axis])
            ]
        )
    else:
        # General case: pick some (possibly consecutive, even non-unique)
        # indices at which we will split the tensor, along the given axis.
        splits = sorted(draw(
            st.lists(elements=st.integers(0, tensor.shape[axis]), max_size=4)
        ) + [0, tensor.shape[axis]])
        return (
            axis,
            np.array(np.diff(splits), dtype=np.int32),
            [
                tensor.take(range(splits[i], splits[i + 1]), axis=axis)
                for i in range(len(splits) - 1)
            ],
        )


class TestConcatSplitOps(hu.HypothesisTestCase):
    @given(tensor_splits=_tensor_splits(),
           **hu.gcs)
    def test_concat(self, tensor_splits, gc, dc):
        axis, _, splits = tensor_splits

        op = core.CreateOperator(
            "Concat",
            ['X_{}'.format(i) for i in range(len(splits))],
            ['concat_result', 'split_info'],
            axis=axis
        )

        self.assertReferenceChecks(
            gc, op, splits, lambda *splits: (
                np.concatenate(splits, axis=axis),
                np.array([a.shape[axis] for a in splits])
            )
        )
        self.assertDeviceChecks(dc, op, splits, [0, 1])
        self.assertGradientChecks(gc, op, splits, 0, [0])

    @given(tensor_splits=_tensor_splits(add_axis=True),
           **hu.gcs)
    def test_concat_add_axis(self, tensor_splits, gc, dc):
        axis, _, splits = tensor_splits

        op = core.CreateOperator(
            "Concat",
            ['X_{}'.format(i) for i in range(len(splits))],
            ['concat_result', 'split_info'],
            axis=axis,
            add_axis=1
        )

        self.assertReferenceChecks(
            gc, op, splits, lambda *splits: (
                np.concatenate(
                    [np.expand_dims(a, axis) for a in splits],
                    axis=axis
                ),
                np.array([1] * len(splits))
            )
        )
        self.assertDeviceChecks(dc, op, splits, [0, 1])
        for i in range(len(splits)):
            self.assertGradientChecks(gc, op, splits, i, [0])

    @given(tensor_splits=_tensor_splits(),
           split_as_arg=st.booleans(),
           **hu.gcs)
    def test_split(self, tensor_splits, split_as_arg, gc, dc):
        axis, split_info, splits = tensor_splits

        split_as_arg = True

        if split_as_arg:
            input_names = ['input']
            input_tensors = [np.concatenate(splits, axis=axis)]
            kwargs = dict(axis=axis, split=split_info)
        else:
            input_names = ['input', 'split']
            input_tensors = [np.concatenate(splits, axis=axis), split_info]
            kwargs = dict(axis=axis)

        op = core.CreateOperator(
            "Split",
            input_names,
            ['X_{}'.format(i) for i in range(len(split_info))],
            **kwargs
        )

        def split_ref(input, split=split_info):
            s = np.cumsum([0] + list(split))
            return [
                np.array(input.take(np.arange(s[i], s[i + 1]), axis=axis))
                for i in range(len(split))
            ]
        outputs_with_grad = range(len(split_info))
        self.assertReferenceChecks(gc, op, input_tensors, split_ref)
        self.assertDeviceChecks(dc, op, input_tensors, outputs_with_grad)
        self.assertGradientChecks(gc, op, input_tensors, 0, outputs_with_grad)

    @given(
        inputs=hu.lengths_tensor(
            dtype=np.float32,
            min_value=1,
            max_value=5,
            allow_empty=True,
        ),
        **hu.gcs_cpu_only
    )
    def test_split_by_lengths(self, inputs, gc, dc):
        data, lengths = inputs
        len_len = len(lengths)

        def _find_factor_simple(x):
            for i in [2, 3, 5]:
                if x % i == 0:
                    return i
            return x

        num_output = _find_factor_simple(len_len)
        axis = 0
        op = core.CreateOperator(
            "SplitByLengths",
            ["data", "lengths"],
            ['X_{}'.format(i) for i in range(num_output)],
            axis=axis,
        )

        def split_by_lengths_ref(data, lengths, num_output=num_output, axis=0):
            idxs = np.cumsum([0] + list(lengths)).astype(np.int32)
            return [
                np.array(
                    data.take(
                        np.arange(
                            idxs[i * len_len // num_output],
                            idxs[(i + 1) * len_len // num_output]
                        ),
                        axis=axis
                    )
                ) for i in range(num_output)
            ]
        outputs_with_grad = range(num_output)
        input_tensors = [data, lengths]
        self.assertReferenceChecks(
            hu.cpu_do, op, input_tensors, split_by_lengths_ref)
        self.assertDeviceChecks(dc, op, input_tensors, outputs_with_grad)
        self.assertGradientChecks(
            hu.cpu_do, op, input_tensors, 0, outputs_with_grad)


if __name__ == "__main__":
    unittest.main()
