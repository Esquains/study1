# Owner(s): ["module: dynamo"]

# this file is autogenerated via gen_ufuncs.py
# do not edit manually!
import numpy as np

from torch._numpy._ufuncs import *  # noqa: F403
from torch._numpy.testing import assert_allclose
from torch.testing._internal.common_utils import run_tests, TestCase


class TestBinaryUfuncBasic(TestCase):
    def test_add(self):
        assert_allclose(np.add(0.5, 0.6).item(), add(0.5, 0.6), atol=1e-7, check_dtype=False)

    def test_arctan2(self):
        assert_allclose(
            np.arctan2(0.5, 0.6).item(), arctan2(0.5, 0.6), atol=1e-7, check_dtype=False
        )

    def test_bitwise_and(self):
        assert_allclose(
            np.bitwise_and(5, 6).item(), bitwise_and(5, 6), atol=1e-7, check_dtype=False
        )

    def test_bitwise_or(self):
        assert_allclose(
            np.bitwise_or(5, 6).item(), bitwise_or(5, 6), atol=1e-7, check_dtype=False
        )

    def test_bitwise_xor(self):
        assert_allclose(
            np.bitwise_xor(5, 6).item(), bitwise_xor(5, 6), atol=1e-7, check_dtype=False
        )

    def test_copysign(self):
        assert_allclose(
            np.copysign(0.5, 0.6).item(), copysign(0.5, 0.6), atol=1e-7, check_dtype=False
        )

    def test_divide(self):
        assert_allclose(
            np.divide(0.5, 0.6).item(), divide(0.5, 0.6), atol=1e-7, check_dtype=False
        )

    def test_equal(self):
        assert_allclose(
            np.equal(0.5, 0.6).item(), equal(0.5, 0.6), atol=1e-7, check_dtype=False
        )

    def test_float_power(self):
        assert_allclose(
            np.float_power(0.5, 0.6).item(),
            float_power(0.5, 0.6),
            atol=1e-7,
            check_dtype=False,
        )

    def test_floor_divide(self):
        assert_allclose(
            np.floor_divide(0.5, 0.6).item(),
            floor_divide(0.5, 0.6),
            atol=1e-7,
            check_dtype=False,
        )

    def test_fmax(self):
        assert_allclose(np.fmax(0.5, 0.6).item(), fmax(0.5, 0.6), atol=1e-7, check_dtype=False)

    def test_fmin(self):
        assert_allclose(np.fmin(0.5, 0.6).item(), fmin(0.5, 0.6), atol=1e-7, check_dtype=False)

    def test_fmod(self):
        assert_allclose(np.fmod(0.5, 0.6).item(), fmod(0.5, 0.6), atol=1e-7, check_dtype=False)

    def test_gcd(self):
        assert_allclose(np.gcd(5, 6).item(), gcd(5, 6), atol=1e-7, check_dtype=False)

    def test_greater(self):
        assert_allclose(
            np.greater(0.5, 0.6).item(), greater(0.5, 0.6), atol=1e-7, check_dtype=False
        )

    def test_greater_equal(self):
        assert_allclose(
            np.greater_equal(0.5, 0.6).item(),
            greater_equal(0.5, 0.6),
            atol=1e-7,
            check_dtype=False,
        )

    def test_heaviside(self):
        assert_allclose(
            np.heaviside(0.5, 0.6).item(), heaviside(0.5, 0.6), atol=1e-7, check_dtype=False
        )

    def test_hypot(self):
        assert_allclose(
            np.hypot(0.5, 0.6).item(), hypot(0.5, 0.6), atol=1e-7, check_dtype=False
        )

    def test_lcm(self):
        assert_allclose(np.lcm(5, 6).item(), lcm(5, 6), atol=1e-7, check_dtype=False)

    def test_ldexp(self):
        assert_allclose(np.ldexp(0.5, 6).item(), ldexp(0.5, 6), atol=1e-7, check_dtype=False)

    def test_left_shift(self):
        assert_allclose(
            np.left_shift(5, 6).item(), left_shift(5, 6), atol=1e-7, check_dtype=False
        )

    def test_less(self):
        assert_allclose(np.less(0.5, 0.6).item(), less(0.5, 0.6), atol=1e-7, check_dtype=False)

    def test_less_equal(self):
        assert_allclose(
            np.less_equal(0.5, 0.6).item(), less_equal(0.5, 0.6), atol=1e-7, check_dtype=False
        )

    def test_logaddexp(self):
        assert_allclose(
            np.logaddexp(0.5, 0.6).item(), logaddexp(0.5, 0.6), atol=1e-7, check_dtype=False
        )

    def test_logaddexp2(self):
        assert_allclose(
            np.logaddexp2(0.5, 0.6).item(), logaddexp2(0.5, 0.6), atol=1e-7, check_dtype=False
        )

    def test_logical_and(self):
        assert_allclose(
            np.logical_and(0.5, 0.6).item(),
            logical_and(0.5, 0.6),
            atol=1e-7,
            check_dtype=False,
        )

    def test_logical_or(self):
        assert_allclose(
            np.logical_or(0.5, 0.6).item(), logical_or(0.5, 0.6), atol=1e-7, check_dtype=False
        )

    def test_logical_xor(self):
        assert_allclose(
            np.logical_xor(0.5, 0.6).item(),
            logical_xor(0.5, 0.6),
            atol=1e-7,
            check_dtype=False,
        )

    def test_matmul(self):
        assert_allclose(
            np.matmul([0.5], [0.6]).item(), matmul([0.5], [0.6]), atol=1e-7, check_dtype=False
        )

    def test_maximum(self):
        assert_allclose(
            np.maximum(0.5, 0.6).item(), maximum(0.5, 0.6), atol=1e-7, check_dtype=False
        )

    def test_minimum(self):
        assert_allclose(
            np.minimum(0.5, 0.6).item(), minimum(0.5, 0.6), atol=1e-7, check_dtype=False
        )

    def test_remainder(self):
        assert_allclose(
            np.remainder(0.5, 0.6).item(), remainder(0.5, 0.6), atol=1e-7, check_dtype=False
        )

    def test_multiply(self):
        assert_allclose(
            np.multiply(0.5, 0.6).item(), multiply(0.5, 0.6), atol=1e-7, check_dtype=False
        )

    def test_nextafter(self):
        assert_allclose(
            np.nextafter(0.5, 0.6).item(), nextafter(0.5, 0.6), atol=1e-7, check_dtype=False
        )

    def test_not_equal(self):
        assert_allclose(
            np.not_equal(0.5, 0.6).item(), not_equal(0.5, 0.6), atol=1e-7, check_dtype=False
        )

    def test_power(self):
        assert_allclose(
            np.power(0.5, 0.6).item(), power(0.5, 0.6), atol=1e-7, check_dtype=False
        )

    def test_right_shift(self):
        assert_allclose(
            np.right_shift(5, 6).item(), right_shift(5, 6), atol=1e-7, check_dtype=False
        )

    def test_subtract(self):
        assert_allclose(
            np.subtract(0.5, 0.6).item(), subtract(0.5, 0.6), atol=1e-7, check_dtype=False
        )


if __name__ == "__main__":
    run_tests()
