from __future__ import absolute_import, division, print_function, unicode_literals

import collections

import caffe2.python.hypothesis_test_util as hu
import hypothesis.strategies as st
import numpy as np
from caffe2.python import core, dyndep
from dnnlowp_test_utils import check_quantized_results_close
from hypothesis import given


dyndep.InitOpsLibrary("//caffe2/caffe2/operators/quantized/server:dnnlowp_ops")


class DNNLowPFullyConnectedAcc16OpTest(hu.HypothesisTestCase):
    # correctness test with no quantization error in inputs
    # fbgemm currently only supports N a multiple of 64
    @given(
        input_channels=st.sampled_from((32, 64)),
        output_channels=st.sampled_from((64, 128, 256)),
        batch_size=st.sampled_from((32, 64, 128, 256)),
        in_quantized=st.booleans(),
        out_quantized=st.booleans(),
        **hu.gcs_cpu_only
    )
    def test_dnnlowp_fully_connected_acc16_int(
        self,
        input_channels,
        output_channels,
        batch_size,
        in_quantized,
        out_quantized,
        gc,
        dc,
    ):
        # X and W have scale 1, so exactly represented after quantization
        # This was made sure by having at least one 0 and one 255 for unsigned
        # 8-bit tensors, and at least one -128 and one 127 for signed 8-bit
        # tensors.
        # Since fbgemm_acc16 accumulates to 16-bit, To avoid overflow, we use
        # small numbers except for those 0, 255, -128, and 127, for this test
        # We also make sure 255, -128, or 127 are not multiplied together by
        # putting them in different input channels and the corresponding input
        # channel in other matrix is 0.
        # For example, we put 255 in input channel 1 in X, so we make the
        # corresponding input channel in W all zeros.
        X_min = -77
        X_max = X_min + 255
        X = np.round(np.random.rand(batch_size, input_channels) * 4 + X_min)
        X = X.astype(np.float32)
        X[:, 0] = X_min
        X[0, 1] = X_max

        W_min = -100
        W_max = W_min + 255
        W = np.round(
            np.random.rand(output_channels, input_channels) * 4 - 2 + W_min + 128
        )
        W = W.astype(np.float32)
        W[0, 0] = W_min
        W[1, 0] = W_max
        W[:, 1] = W_min + 128

        # No input quantization error in bias
        b = np.round(np.random.randn(output_channels)).astype(np.float32)

        Output = collections.namedtuple("Output", ["Y", "op_type", "engine"])
        outputs = []

        op_engine_list = [
            ("FC", ""),
            ("FC", "DNNLOWP_ACC16"),
            ("Int8FC", "DNNLOWP_ACC16"),
        ]

        for op_type, engine in op_engine_list:
            net = core.Net("test_net")

            do_quantize = "DNNLOWP" in engine and in_quantized
            do_dequantize = "DNNLOWP" in engine and out_quantized

            if do_quantize:
                quantize = core.CreateOperator(
                    "Quantize", ["X"], ["X_q"], engine="DNNLOWP", device_option=gc
                )
                net.Proto().op.extend([quantize])

            fc = core.CreateOperator(
                op_type,
                ["X_q" if do_quantize else "X", "W", "b"],
                ["Y_q" if do_dequantize else "Y"],
                dequantize_output=(0 if do_dequantize else 1),
                engine=engine,
                device_option=gc,
            )
            net.Proto().op.extend([fc])

            if do_dequantize:
                dequantize = core.CreateOperator(
                    "Dequantize", ["Y_q"], ["Y"], engine="DNNLOWP", device_option=gc
                )
                net.Proto().op.extend([dequantize])

            self.ws.create_blob("X").feed(X, device_option=gc)
            self.ws.create_blob("W").feed(W, device_option=gc)
            self.ws.create_blob("b").feed(b, device_option=gc)
            self.ws.run(net)
            outputs.append(
                Output(Y=self.ws.blobs["Y"].fetch(), op_type=op_type, engine=engine)
            )

        check_quantized_results_close(outputs)

    @given(
        input_channels=st.sampled_from((2, 2)),
        output_channels=st.sampled_from((4, 4)),
        batch_size=st.sampled_from((1, 1)),
        nbits_in_non_outlier=st.sampled_from((0, 6)),
        in_quantized=st.booleans(),
        out_quantized=st.booleans(),
        **hu.gcs_cpu_only
    )
    def test_dnnlowp_fully_connected_acc16_outlier(
        self,
        input_channels,
        output_channels,
        batch_size,
        nbits_in_non_outlier,
        in_quantized,
        out_quantized,
        gc,
        dc,
    ):
        # X and W have scale 1, so exactly represented after quantization
        # This was made sure by having at least one 0 and one 255 for unsigned
        # 8-bit tensors, and at least one -128 and one 127 for signed 8-bit
        # tensors.
        # Since fbgemm_acc16 accumulates to 16-bit, To avoid overflow, we use
        # small numbers except for those 0, 255, -128, and 127, for this test
        # We also make sure 255, -128, or 127 are not multiplied together by
        # putting them in different input channels and the corresponding input
        # channel in other matrix is 0.
        # For example, we put 255 in input channel 1 in X, so we make the
        # corresponding input channel in W all zeros.
        X_min = -77
        X_max = X_min + 255
        X = np.round(np.random.rand(batch_size, input_channels) * 4 + X_min)
        X = X.astype(np.float32)
        X[:, 0] = X_min
        X[0, 1] = X_max

        W_min = -100
        W_max = W_min + 255
        W = np.round(
            np.random.rand(output_channels, input_channels) * 4 - 2 + W_min + 128
        )
        W = W.astype(np.float32)
        W[0, 0] = W_min
        W[1, 0] = W_max
        W[:, 1] = W_min + 128
        # No input quantization error in bias
        b = np.round(np.random.randn(output_channels)).astype(np.float32)
        Output = collections.namedtuple("Output", ["Y", "op_type", "engine"])
        outputs = []

        op_engine_list = [
            ("FC", ""),
            ("FC", "DNNLOWP_ACC16"),
            ("Int8FC", "DNNLOWP_ACC16"),
        ]

        for op_type, engine in op_engine_list:
            net = core.Net("test_net")

            do_quantize = "DNNLOWP" in engine and in_quantized
            do_dequantize = "DNNLOWP" in engine and out_quantized

            if do_quantize:
                quantize = core.CreateOperator(
                    "Quantize", ["X"], ["X_q"], engine="DNNLOWP", device_option=gc
                )
                net.Proto().op.extend([quantize])

            fc = core.CreateOperator(
                op_type,
                ["X_q" if do_quantize else "X", "W", "b"],
                ["Y_q" if do_dequantize else "Y"],
                dequantize_output=(0 if do_dequantize else 1),
                engine=engine,
                nbits_in_non_outlier=nbits_in_non_outlier,
                device_option=gc,
            )
            net.Proto().op.extend([fc])

            if do_dequantize:
                dequantize = core.CreateOperator(
                    "Dequantize", ["Y_q"], ["Y"], engine="DNNLOWP", device_option=gc
                )
                net.Proto().op.extend([dequantize])

            self.ws.create_blob("X").feed(X, device_option=gc)
            self.ws.create_blob("W").feed(W, device_option=gc)
            self.ws.create_blob("b").feed(b, device_option=gc)
            self.ws.run(net)
            outputs.append(
                Output(Y=self.ws.blobs["Y"].fetch(), op_type=op_type, engine=engine)
            )

        check_quantized_results_close(outputs)
