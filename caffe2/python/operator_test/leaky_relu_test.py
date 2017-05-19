from __future__ import absolute_import
from __future__ import division
from __future__ import print_function

import numpy as np
from hypothesis import given, assume
import hypothesis.strategies as st
from itertools import izip

from caffe2.python import core, model_helper
import caffe2.python.hypothesis_test_util as hu


class TestLeakyRelu(hu.HypothesisTestCase):

    def _get_inputs(self, N, C, H, W, order):
        input_data = np.random.rand(N, C, H, W).astype(np.float32)

        # default step size is 0.05
        input_data[np.logical_and(
            input_data >= 0, input_data <= 0.051)] = 0.051
        input_data[np.logical_and(
            input_data <= 0, input_data >= -0.051)] = -0.051

        if order == 'NHWC':
            input_data = np.transpose(input_data, axes=(0, 2, 3, 1))

        return input_data,

    def _get_op(self, device_option, alpha, order, inplace=False):
        outputs = ['output' if not inplace else "input"]
        op = core.CreateOperator(
            'LeakyRelu',
            ['input'],
            outputs,
            alpha=alpha,
            device_option=device_option)
        return op

    def _feed_inputs(self, input_blobs, device_option):
        names = ['input', 'scale', 'bias']
        for name, blob in izip(names, input_blobs):
            self.ws.create_blob(name).feed(blob, device_option=device_option)

    @given(gc=hu.gcs['gc'],
           dc=hu.gcs['dc'],
           N=st.integers(2, 3),
           C=st.integers(2, 3),
           H=st.integers(2, 3),
           W=st.integers(2, 3),
           alpha=st.floats(0, 1),
           order=st.sampled_from(['NCHW', 'NHWC']),
           seed=st.integers(0, 1000))
    def test_leaky_relu_gradients(self, gc, dc, N, C, H, W, order, alpha, seed):
        np.random.seed(seed)

        op = self._get_op(
            device_option=gc,
            alpha=alpha,
            order=order)
        input_blobs = self._get_inputs(N, C, H, W, order)

        self.assertDeviceChecks(dc, op, input_blobs, [0])
        self.assertGradientChecks(gc, op, input_blobs, 0, [0])

    @given(gc=hu.gcs['gc'],
           dc=hu.gcs['dc'],
           N=st.integers(2, 10),
           C=st.integers(3, 10),
           H=st.integers(5, 10),
           W=st.integers(7, 10),
           alpha=st.floats(0, 1),
           seed=st.integers(0, 1000))
    def test_leaky_relu_layout(self, gc, dc, N, C, H, W, alpha, seed):
        outputs = {}
        for order in ('NCHW', 'NHWC'):
            np.random.seed(seed)
            input_blobs = self._get_inputs(N, C, H, W, order)
            self._feed_inputs(input_blobs, device_option=gc)
            op = self._get_op(
                device_option=gc,
                alpha=alpha,
                order=order)
            self.ws.run(op)
            outputs[order] = self.ws.blobs['output'].fetch()
        np.testing.assert_allclose(
            outputs['NCHW'],
            outputs['NHWC'].transpose((0, 3, 1, 2)),
            atol=1e-4,
            rtol=1e-4)

    @given(gc=hu.gcs['gc'],
           dc=hu.gcs['dc'],
           N=st.integers(2, 10),
           C=st.integers(3, 10),
           H=st.integers(5, 10),
           W=st.integers(7, 10),
           order=st.sampled_from(['NCHW', 'NHWC']),
           alpha=st.floats(0, 1),
           seed=st.integers(0, 1000),
           inplace=st.booleans())
    def test_leaky_relu_reference_check(self, gc, dc, N, C, H, W, order, alpha,
                                        seed, inplace):
        np.random.seed(seed)

        if order != "NCHW":
            assume(not inplace)

        inputs = self._get_inputs(N, C, H, W, order)
        op = self._get_op(
            device_option=gc,
            alpha=alpha,
            order=order,
            inplace=inplace)

        def ref(input_blob):
            result = input_blob.copy()
            result[result < 0] *= alpha
            return result,

        self.assertReferenceChecks(gc, op, inputs, ref)

    @given(gc=hu.gcs['gc'],
           dc=hu.gcs['dc'],
           N=st.integers(2, 10),
           C=st.integers(3, 10),
           H=st.integers(5, 10),
           W=st.integers(7, 10),
           order=st.sampled_from(['NCHW', 'NHWC']),
           alpha=st.floats(0, 1),
           seed=st.integers(0, 1000))
    def test_leaky_relu_device_check(self, gc, dc, N, C, H, W, order, alpha,
                                     seed):
        np.random.seed(seed)

        inputs = self._get_inputs(N, C, H, W, order)
        op = self._get_op(
            device_option=gc,
            alpha=alpha,
            order=order)

        self.assertDeviceChecks(dc, op, inputs, [0])

    @given(N=st.integers(2, 10),
           C=st.integers(3, 10),
           H=st.integers(5, 10),
           W=st.integers(7, 10),
           order=st.sampled_from(['NCHW', 'NHWC']),
           alpha=st.floats(0, 1),
           seed=st.integers(0, 1000))
    def test_leaky_relu_model_helper_helper(self, N, C, H, W, order, alpha, seed):
        np.random.seed(seed)
        arg_scope = {'order': order}
        model = model_helper.ModelHelper(name="test_model", arg_scope=arg_scope)
        model.LeakyRelu(
            'input',
            'output',
            alpha=alpha)

        input_blob = np.random.rand(N, C, H, W).astype(np.float32)
        if order == 'NHWC':
            input_blob = np.transpose(input_blob, axes=(0, 2, 3, 1))

        self.ws.create_blob('input').feed(input_blob)

        self.ws.create_net(model.param_init_net).run()
        self.ws.create_net(model.net).run()

        output_blob = self.ws.blobs['output'].fetch()
        if order == 'NHWC':
            output_blob = np.transpose(output_blob, axes=(0, 3, 1, 2))

        assert output_blob.shape == (N, C, H, W)


if __name__ == '__main__':
    import unittest
    unittest.main()
