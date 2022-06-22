#define TORCH_ASSERT_NO_OPERATORS
#include <ATen/Dispatch.h>
#include <ATen/native/Copy.h>
#include <ATen/native/TensorIterator.h>
#include <ATen/native/cpu/Loops.h>
#include <c10/util/TypeCast.h>
#include <ATen/native/cpu/zmath.h>
#include <ATen/TensorIteratorInternal.h>

namespace at {
namespace native {
inline namespace CPU_CAPABILITY {
void neg_kernel(TensorIteratorBase &iter);
void conj_kernel(TensorIteratorBase &iter);
} // namespace CPU_CAPABILITY

namespace {

inline void float_bfloat16_copy_kernel(TensorIteratorBase &iter, bool requires_neg) {
  if (iter.dtype(1) == ScalarType::BFloat16 && iter.dtype(0) == ScalarType::Float) {
    using src_scalar = at::BFloat16;
    using dst_scalar = float;
    using src_t = std::tuple<Vectorized<src_scalar>>;
    using dst_t = std::tuple<Vectorized<dst_scalar>, Vectorized<dst_scalar>>;
    cpu_kernel_vec(
      iter,
      [=](src_scalar a) -> dst_scalar { return a; },
      [=](src_t a) -> dst_t {
        Vectorized<src_scalar> a_vec;
        std::tie(a_vec) = a;
        return convert_bfloat16_float(requires_neg ? a_vec.neg() : a_vec); });
  } else if (iter.dtype(1) == ScalarType::Float && iter.dtype(0) == ScalarType::BFloat16) {
    using src_scalar = float;
    using dst_scalar = at::BFloat16;
    using src_t = std::tuple<Vectorized<src_scalar>, Vectorized<src_scalar>>;
    using dst_t = std::tuple<Vectorized<dst_scalar>>;
    cpu_kernel_vec(
      iter,
      [=](src_scalar a) -> dst_scalar { return a; },
      [=](src_t a) -> dst_t {
        Vectorized<src_scalar> a_vec0, a_vec1;
        std::tie(a_vec0, a_vec1) = a;
        auto a_vec = requires_neg ? convert_float_bfloat16(a_vec0.neg(), a_vec1.neg()) :
          convert_float_bfloat16(a_vec0, a_vec1);
        return std::make_tuple(a_vec); });
  }
}

void direct_copy_kernel(TensorIteratorBase &iter) {
  // TODO: we don't actually need separate instantiations per dtype;
  // we only need a separate instantiation per dtype size. This would
  // probably save us a little bit of code size here
  // TODO: not sure if optimizer is able to compile two levels of
  // conditionals into a single jump table.  We should have a
  // single jump table here; might be worth just writing out the
  // dispatch statement by hand instead of using AT_DISPATCH
  ScalarType dtype = iter.dtype(0);
  if (isQIntType(dtype)) {
    AT_DISPATCH_QINT_TYPES(dtype, "copy_kernel", [&] {
      cpu_kernel_vec(
          iter,
          [=](scalar_t a) -> scalar_t { return a; },
          [=](Vectorized<scalar_t> a) -> Vectorized<scalar_t> { return a; });
    });
  } else if (dtype == ScalarType::ComplexHalf) {
    cpu_kernel(iter, [=](c10::complex<at::Half> a) -> c10::complex<at::Half> { return a; });
  } else {
    AT_DISPATCH_ALL_TYPES_AND_COMPLEX_AND3(
        kBool, kHalf, kBFloat16, dtype, "copy_kernel", [&] {
      cpu_kernel_vec(
          iter,
          [=](scalar_t a) -> scalar_t { return a; },
          [=](Vectorized<scalar_t> a) -> Vectorized<scalar_t> { return a; });
    });
  }
}

void neg_conj_kernel(TensorIteratorBase &iter) {
  // fused a = b.neg().conj_physical()
  AT_DISPATCH_COMPLEX_TYPES(iter.common_dtype(), "neg_conj_cpu", [&] {
    cpu_kernel_vec(
        iter,
        [=](scalar_t a) -> scalar_t { return -conj_impl(a); },
        [=](Vectorized<scalar_t> a) -> Vectorized<scalar_t> { return a.neg().conj(); });
  });
}

void copy_same_dtype(TensorIteratorBase &iter, bool requires_conj, bool requires_neg) {
  if (requires_neg) {
    // This case should never actually happen since currently there's no way to get a complex tensor
    // with negative bit.
    if (requires_conj) {
      neg_conj_kernel(iter);
    } else {
      neg_kernel(iter);
    }
  } else {
    if (requires_conj) {
      conj_kernel(iter);
    } else {
      direct_copy_kernel(iter);
    }
  }
}

void copy_kernel(TensorIterator& iter, bool /*non_blocking*/) {
  ScalarType dtype = iter.dtype(0);
  const bool requires_conj = (
      isComplexType(dtype) && (iter.tensor_base(0).is_conj() != iter.tensor_base(1).is_conj()));
  const bool requires_neg = (iter.tensor_base(0).is_neg() != iter.tensor_base(1).is_neg());

  if (dtype == iter.dtype(1)) {
    copy_same_dtype(iter, requires_conj, requires_neg);
  } else if (!requires_conj && ((iter.dtype(1) == ScalarType::BFloat16 && iter.dtype(0) == ScalarType::Float) ||
              (iter.dtype(1) == ScalarType::Float && iter.dtype(0) == ScalarType::BFloat16))) {
    float_bfloat16_copy_kernel(iter, requires_neg);
  } else {
    AT_DISPATCH_ALL_TYPES_AND_COMPLEX_AND4(ScalarType::ComplexHalf, ScalarType::Half, ScalarType::Bool, ScalarType::BFloat16, dtype, "copy_", [&] {
      using dest_t = scalar_t;
      AT_DISPATCH_ALL_TYPES_AND_COMPLEX_AND4(ScalarType::ComplexHalf, ScalarType::Half, ScalarType::Bool, ScalarType::BFloat16, iter.dtype(1), "copy_", [&] {
        // Note (@zasdfgbnm):
        //
        // The code below can not be simplified as
        //    cpu_kernel(iter, c10::static_cast_with_inter_type<dest_t, scalar_t>::apply);
        //
        // because this would force the compiler to instantiate the inline function and generate a function call in the loop
        // instead of inlining it, making all the optimizations like vectorization impossible.
        // You can verify this by looking the the symbols of `libtorch_cpu.so`:
        //
        //    readelf -Ws libtorch_cpu.so | grep static_cast_with_inter_type
        //
        // If done correctly, the above command should have no output.
        //
        // See: https://github.com/pytorch/pytorch/issues/31271
        cpu_kernel(iter, [](scalar_t src) -> dest_t {
          return c10::static_cast_with_inter_type<dest_t, scalar_t>::apply(src); });
      });
    });

    if (requires_conj || requires_neg) {
      // This inplace "copy" will perform any missing neg or conj operations
      auto self = iter.tensor_base(0);
      auto iter = TensorIterator::unary_op(self, self);
      copy_same_dtype(iter, requires_conj, requires_neg);
    }
  }
}

} // anonymous namespace

REGISTER_DISPATCH(copy_stub, &copy_kernel);

} // namespace native
} // namespace at
