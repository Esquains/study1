#pragma once

#include <ATen/core/ATenGeneral.h>
#include <ATen/core/Generator.h>
#include <c10/core/StorageImpl.h>
#include <c10/core/UndefinedTensorImpl.h>

#include <c10/core/ScalarType.h>
#include <ATen/Formatting.h>
#include <c10/util/ArrayRef.h>
#include <c10/util/Exception.h>

#include <algorithm>
#include <sstream>
#include <typeinfo>
#include <numeric>
#include <memory>

#define AT_DISALLOW_COPY_AND_ASSIGN(TypeName) \
  TypeName(const TypeName&) = delete; \
  void operator=(const TypeName&) = delete

namespace at {

CAFFE2_API int _crash_if_asan(int);

// TODO: This unwrapping code is ONLY used for TH bindings; once TH goes
// away, we can delete this function
static inline TensorImpl* checked_dense_tensor_unwrap(const Tensor& expr, const char * name, int pos, const char * api, bool allowNull, DeviceType device_type, ScalarType scalar_type) {
  if(allowNull && !expr.defined()) {
    return nullptr;
  }
  if (expr.layout() != Layout::Strided) {
    AT_ERROR("Expected dense tensor but got ", expr.layout(),
             " for argument #", pos, " '", name, "' in call to ", api);
  }
  if (expr.device().type() != device_type) {
    AT_ERROR("Expected object of device type ", device_type, " but got device type ", expr.device().type(),
             " for argument #", pos, " '", name, "' in call to ", api);
  }
  if (expr.scalar_type() != scalar_type) {
    AT_ERROR("Expected object of scalar type ", scalar_type, " but got scalar type ", expr.scalar_type(),
             " for argument #", pos, " '", name, "' in call to ", api);
  }
  return expr.unsafeGetTensorImpl();
}

// Converts a TensorList (i.e. ArrayRef<Tensor> to vector of TensorImpl*)
// NB: This is ONLY used by legacy TH bindings, and ONLY used by cat.
// Once cat is ported entirely to ATen this can be deleted!
static inline std::vector<TensorImpl*> checked_dense_tensor_list_unwrap(ArrayRef<Tensor> tensors, const char * name, int pos, DeviceType device_type, ScalarType scalar_type) {
  std::vector<TensorImpl*> unwrapped;
  unwrapped.reserve(tensors.size());
  for (unsigned int i = 0; i < tensors.size(); ++i) {
    const auto& expr = tensors[i];
    if (expr.layout() != Layout::Strided) {
      AT_ERROR("Expected dense tensor but got ", expr.layout(),
               " for sequence element ", i , " in sequence argument at position #", pos, " '", name, "'");
    }
    if (expr.device().type() != device_type) {
      AT_ERROR("Expected object of device type ", device_type, " but got device type ", expr.device().type(),
               " for sequence element ", i , " in sequence argument at position #", pos, " '", name, "'");
    }
    if (expr.scalar_type() != scalar_type) {
      AT_ERROR("Expected object of scalar type ", scalar_type, " but got scalar type ", expr.scalar_type(),
               " for sequence element ", i , " in sequence argument at position #", pos, " '", name, "'");
    }
    unwrapped.emplace_back(expr.unsafeGetTensorImpl());
  }
  return unwrapped;
}

template <size_t N>
std::array<int64_t, N> check_intlist(ArrayRef<int64_t> list, const char * name, int pos) {
  if (list.empty()) {
    // TODO: is this necessary?  We used to treat nullptr-vs-not in IntList differently
    // with strides as a way of faking optional.
    list = {};
  }
  auto res = std::array<int64_t, N>();
  if (list.size() == 1 && N > 1) {
    res.fill(list[0]);
    return res;
  }
  if (list.size() != N) {
    AT_ERROR("Expected a list of ", N, " ints but got ", list.size(), " for argument #", pos, " '", name, "'");
  }
  std::copy_n(list.begin(), N, res.begin());
  return res;
}

inline int64_t sum_intlist(ArrayRef<int64_t> list) {
  return std::accumulate(list.begin(), list.end(), 0ll);
}

template<typename C, typename std::enable_if<std::is_integral<typename C::value_type>::value, int>::type = 0>
inline int64_t prod_intlist(const C &container){
    return std::accumulate(container.begin(), container.end(), static_cast<int64_t>(1), std::multiplies<int64_t>());
}

template<typename Iter,
typename std::enable_if<std::is_integral<typename std::iterator_traits<Iter>::value_type>::value, int>::type = 0>
inline int64_t prod_intlist(Iter begin, Iter end){
    return std::accumulate(begin, end, static_cast<int64_t>(1), std::multiplies<int64_t>());
}
/**
 * Utility function to static cast input Generator* to
 * the backend generator type (CPU/CUDAGeneratorImpl etc.)
 */
template <typename T>
static inline T * check_generator(c10::optional<Generator> gen) {
  TORCH_CHECK(gen.has_value(), "Expected Generator but received nullopt");
  TORCH_CHECK(gen->defined(), "Generator with undefined implementation is not allowed");
  TORCH_CHECK(T::device_type() == gen->device().type(), "Expected a '", T::device_type(), "' device type for generator but found '", gen->device().type(), "'");
  return gen->get<T>();
}

/**
 * Utility function used in tensor implementations, which
 * supplies the default generator to tensors, if an input generator
 * is not supplied. The input Generator* is also static casted to
 * the backend generator type (CPU/CUDAGeneratorImpl etc.)
 */
template <typename T>
static inline T* get_generator_or_default(const c10::optional<Generator>& gen, const Generator& default_gen) {
  return gen.has_value() && gen->defined() ? check_generator<T>(gen) : check_generator<T>(default_gen);
}

} // at
