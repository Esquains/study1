#pragma once

#include <c10/macros/Macros.h>
#include <cstring>
#include <limits>

C10_CLANG_DIAGNOSTIC_PUSH()
#if C10_CLANG_HAS_WARNING("-Wimplicit-int-float-conversion")
C10_CLANG_DIAGNOSTIC_IGNORE("-Wimplicit-int-float-conversion")
#endif

#define EXP_WIDTH_FP8 5
#define MAN_WIDTH_FP8 2
#define EXP_BIAS_FP8 15

namespace c10 {

/// Constructors

inline C10_HOST_DEVICE Float8_e5m2fnuz::Float8_e5m2fnuz(float value) {
  x = detail::fp8e5m2fnuz_from_fp32_value(value);
}

/// Implicit conversions

inline C10_HOST_DEVICE Float8_e5m2fnuz::operator float() const {
  return detail::fp8e5m2fnuz_to_fp32_value(x);
}

/// Special values helpers

inline C10_HOST_DEVICE bool Float8_e5m2fnuz::isnan() const {
  return x == 0b10000000;
}

inline C10_HOST_DEVICE bool Float8_e5m2fnuz::isinf() const {
  return false;
}

} // namespace c10

namespace std {

template <>
class numeric_limits<c10::Float8_e5m2fnuz> {
 public:
  static constexpr bool is_signed = true;
  static constexpr bool is_integer = false;
  static constexpr bool is_specialized = true;
  static constexpr bool is_exact = false;
  static constexpr bool has_infinity = true;
  static constexpr bool has_quiet_NaN = false;
  static constexpr bool has_signaling_NaN = false;
  static constexpr auto has_denorm = true;
  static constexpr auto has_denorm_loss = true;
  static constexpr auto round_style = numeric_limits<float>::round_style;
  static constexpr bool is_iec559 = false;
  static constexpr bool is_bounded = true;
  static constexpr bool is_modulo = false;
  static constexpr int digits = 3;
  static constexpr int digits10 = 0;
  static constexpr int max_digits10 = 2;
  static constexpr int radix = 2;
  static constexpr int min_exponent = -13;
  static constexpr int min_exponent10 = -4;
  static constexpr int max_exponent = 16;
  static constexpr int max_exponent10 = 4;
  static constexpr auto traps = numeric_limits<float>::traps;
  static constexpr auto tinyness_before =
      numeric_limits<float>::tinyness_before;

  static constexpr c10::Float8_e5m2fnuz min() {
    return c10::Float8_e5m2fnuz(0x4, c10::Float8_e5m2fnuz::from_bits());
  }
  static constexpr c10::Float8_e5m2fnuz max() {
    return c10::Float8_e5m2fnuz(0x7B, c10::Float8_e5m2fnuz::from_bits());
  }
  static constexpr c10::Float8_e5m2fnuz lowest() {
    return c10::Float8_e5m2fnuz(0xFB, c10::Float8_e5m2fnuz::from_bits());
  }
  static constexpr c10::Float8_e5m2fnuz epsilon() {
    return c10::Float8_e5m2fnuz(0x34, c10::Float8_e5m2fnuz::from_bits());
  }
  static constexpr c10::Float8_e5m2fnuz round_error() {
    return c10::Float8_e5m2fnuz(0x38, c10::Float8_e5m2fnuz::from_bits());
  }
  static constexpr c10::Float8_e5m2fnuz infinity() {
    return c10::Float8_e5m2fnuz(0x80, c10::Float8_e5m2fnuz::from_bits());
  }
  static constexpr c10::Float8_e5m2fnuz denorm_min() {
    return c10::Float8_e5m2fnuz(0x01, c10::Float8_e5m2fnuz::from_bits());
  }
};

} // namespace std

C10_CLANG_DIAGNOSTIC_POP()
