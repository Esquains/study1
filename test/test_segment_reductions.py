import numpy as np
import torch
from torch.testing._internal.common_device_type import (
    instantiate_device_type_tests,
    dtypes,
    dtypesIfCUDA,
    onlyCPU,
)
from torch.testing._internal.common_utils import (
    TestCase,
    run_tests,
    gradcheck,
)


class TestSegmentReductions(TestCase):
    def _test_common(
        self,
        reduction,
        device,
        dtype,
        unsafe,
        axis,
        initial_value,
        data_arr,
        lengths_arr,
        expected_arr,
        expected_grad_arr,
        check_backward,
    ):
        lengths = torch.tensor(lengths_arr, device=device)
        data = torch.tensor(
            data_arr,
            device=device,
            dtype=dtype,
            requires_grad=True,
        )
        expected_result = torch.tensor(expected_arr, device=device, dtype=dtype)
        expected_grad = torch.tensor(expected_grad_arr, device=device, dtype=dtype)
        actual_result = torch.segment_reduce(
            data=data,
            reduce=reduction,
            lengths=lengths,
            axis=axis,
            unsafe=unsafe,
            initial=initial_value,
        )
        self.assertEqual(
            expected_result, actual_result, rtol=1e-02, atol=1e-05, equal_nan=True
        )

        if not check_backward:
            return

        # Test backward
        actual_result.sum().backward()
        self.assertEqual(
            expected_grad, data.grad, rtol=1e-02, atol=1e-05, equal_nan=True
        )

        # gradcheck does not work well with bfloat16 or fp16 cpu types
        # also there is small numerical difference with fp32
        if dtype not in [torch.half, torch.bfloat16, torch.float]:
            # gradcheck does not like "nan" input, setting to random 10
            d_non_nan = [
                10 if is_nan else data_arr[index]
                for index, is_nan in enumerate(torch.isnan(data))
            ]
            data = torch.tensor(
                # [10 if v == float("nan") else v for v in data],
                d_non_nan,
                device=device,
                dtype=dtype,
                requires_grad=True,
            )
            self.assertTrue(
                gradcheck(
                    lambda x: torch.segment_reduce(
                        data=x,
                        reduce=reduction,
                        lengths=lengths,
                        axis=axis,
                        unsafe=unsafe,
                        initial=initial_value,
                    ),
                    (data,),
                )
            )

    @dtypesIfCUDA(torch.half, torch.bfloat16, torch.float, torch.double)
    @dtypes(torch.half, torch.bfloat16, torch.float, torch.double)
    def test_simple_1d(self, device, dtype):
        lengths = [1, 2, 3, 0]
        data = [1, float("nan"), 3, 4, 5, 5]
        initial_value = 0

        # TODO: Set this to true once cuda backward support is implemented
        check_backward = device == "cpu"

        for reduction in ("max", "mean"):
            if reduction == "max":
                expected_result = [1, float("nan"), 5, initial_value]
                expected_grad = [1, 1, 0, 0, 0.5, 0.5]
            elif reduction == "mean":
                expected_result = [1, float("nan"), 4.666, initial_value]
                expected_grad = [1.0, 0.5, 0.5, 0.333, 0.333, 0.333]
            for axis in [0, -1]:
                for unsafe in [True, False]:
                    self._test_common(
                        reduction,
                        device,
                        dtype,
                        unsafe,
                        axis,
                        initial_value,
                        data,
                        lengths,
                        expected_result,
                        expected_grad,
                        check_backward,
                    )

    @onlyCPU
    @dtypes(torch.half, torch.bfloat16, torch.float, torch.double)
    def test_multi_d_simple(self, device, dtype):
        initial_value = 0
        axis = 0
        lengths = [1, 2, 3, 0]
        data = [[1, 1], [float("nan"), 1], [3, float("nan")], [4, 1], [5, 1], [5, 1]]

        # TODO: Set this to true once backward is support for multi d
        check_backward = False

        for reduction in ["max", "mean"]:
            if reduction == "max":
                expected_result = [
                    [1, 1],
                    [float("nan"), float("nan")],
                    [5, 1],
                    [initial_value, initial_value],
                ]
                expected_grad = [
                    [1, 1],
                    [1, 0],
                    [0, 1],
                    [0, 0.333],
                    [0.5, 0.333],
                    [0.5, 0.333],
                ]
            elif reduction == "mean":
                expected_result = [
                    [1, 1],
                    [float("nan"), float("nan")],
                    [4.666, 1],
                    [initial_value, initial_value],
                ]
                expected_grad = [
                    [1.0, 1.0],
                    [0.5, 0.5],
                    [0.5, 0.5],
                    [0.333, 0.333],
                    [0.333, 0.333],
                    [0.333, 0.333],
                ]
            for unsafe in [True, False]:
                self._test_common(
                    reduction,
                    device,
                    dtype,
                    unsafe,
                    axis,
                    initial_value,
                    data,
                    lengths,
                    expected_result,
                    expected_grad,
                    check_backward,
                )

    @onlyCPU
    @dtypes(torch.half, torch.bfloat16, torch.float, torch.double)
    def test_multi_d(self, device, dtype):
        initial_value = 0
        axis = 0
        lengths = [0, 2]
        data = np.random.randint(0, 100, size=(2, 2, 5)).tolist()
        expected_grad = []

        # TODO: Set this to true once backward is support for multi d
        check_backward = False

        for reduction in ["max", "mean"]:
            if reduction == "max":
                expected_result = [
                    np.full((2, 5), initial_value).tolist(),
                    np.max(data, axis=0).tolist(),
                ]
            elif reduction == "mean":
                expected_result = [
                    np.full((2, 5), initial_value).tolist(),
                    np.mean(data, axis=0).tolist(),
                ]
            for unsafe in [True, False]:
                self._test_common(
                    reduction,
                    device,
                    dtype,
                    unsafe,
                    axis,
                    initial_value,
                    data,
                    lengths,
                    expected_result,
                    expected_grad,
                    check_backward,
                )


instantiate_device_type_tests(TestSegmentReductions, globals())

if __name__ == "__main__":
    run_tests()
