#include <cmath>
#include <type_traits>
#include <c10/util/BFloat16.h>

namespace at {

// std::isnan isn't performant to use on integral types; it will
// (uselessly) convert to floating point and then do the test.
// This function is.

template <typename T,
          typename std::enable_if<std::is_integral<T>::value, int>::type = 0>
inline bool _isnan(T val) {
  return false;
}

template <typename T,
          typename std::enable_if<std::is_floating_point<T>::value, int>::type = 0>
inline bool _isnan(T val) {
  return std::isnan(val);
}

inline bool _isnan(at::BFloat16 val) { return std::isnan(float(val)); }

} // namespace at
