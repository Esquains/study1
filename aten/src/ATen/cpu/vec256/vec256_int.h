#pragma once

#include "intrinsics.h"
#include "vec256_base.h"

namespace at {
namespace vec256 {
namespace {

#ifdef __AVX2__

struct Vec256i {
protected:
  __m256i values;
public:
  Vec256i() {}
  Vec256i(__m256i v) : values(v) {}
  operator __m256i() const {
    return values;
  }
};

template <>
struct Vec256<int64_t> : public Vec256i {
public:
  static constexpr int size = 4;
  using Vec256i::Vec256i;
  Vec256() {}
  Vec256(int64_t v) { values = _mm256_set1_epi64x(v); }
  template <int64_t mask>
  static Vec256<int64_t> blend(Vec256<int64_t> a, Vec256<int64_t> b) {
    __at_align32__ int64_t tmp_values[size];
    a.store(tmp_values);
    if (mask & 0x01)
      tmp_values[0] = _mm256_extract_epi64(b, 0);
    if (mask & 0x02)
      tmp_values[1] = _mm256_extract_epi64(b, 1);
    if (mask & 0x04)
      tmp_values[2] = _mm256_extract_epi64(b, 2);
    if (mask & 0x08)
      tmp_values[3] = _mm256_extract_epi64(b, 3);
    return load(tmp_values);
  }
  static Vec256<int64_t>
  set(Vec256<int64_t> a, Vec256<int64_t> b, int64_t count = size) {
    switch (count) {
      case 0:
        return a;
      case 1:
        return blend<1>(a, b);
      case 2:
        return blend<3>(a, b);
      case 3:
        return blend<7>(a, b);
    }
    return b;
  }
  static Vec256<int64_t> load(
      const void* ptr,
      int64_t count = size,
      int64_t stride = 1) {
    if (count == size && stride == 1)
      return _mm256_loadu_si256(reinterpret_cast<const __m256i*>(ptr));
    __at_align32__ int64_t tmp_values[size];
    if (stride == 1) {
      std::memcpy(tmp_values, ptr, count * sizeof(int64_t));
    } else {
      for (int64_t i = 0; i < count; i++) {
        tmp_values[i] = reinterpret_cast<const int64_t*>(ptr)[i * stride];
      }
    }
    return load(tmp_values);
  }
  void store(void* ptr, int64_t count = size, int64_t stride = 1) const {
    if (count == size && stride == 1) {
      _mm256_storeu_si256(reinterpret_cast<__m256i*>(ptr), values);
    } else {
      __at_align32__ int64_t tmp_values[size];
      _mm256_storeu_si256(reinterpret_cast<__m256i*>(tmp_values), values);
      if (stride == 1) {
        std::memcpy(ptr, tmp_values, count * sizeof(int64_t));
      } else {
        for (int64_t i = 0; i < count; i++) {
          reinterpret_cast<int64_t*>(ptr)[i * stride] = tmp_values[i];
        }
      }
    }
  }
  const int64_t& operator[](int idx) const  = delete;
  int64_t& operator[](int idx)  = delete;
};
template <>
Vec256<int64_t> inline map(int64_t (*f)(int64_t), Vec256<int64_t> x) = delete;

template <>
Vec256<int64_t> inline abs(Vec256<int64_t> x) {
  auto zero = _mm256_set1_epi64x(0);
  auto is_larger = _mm256_cmpgt_epi64(zero, x);
  auto inverse = _mm256_xor_si256(x, is_larger);
  return _mm256_sub_epi64(inverse, is_larger);
}

template <>
struct Vec256<int32_t> : public Vec256i {
public:
  static constexpr int size = 8;
  using Vec256i::Vec256i;
  Vec256() {}
  Vec256(int32_t v) { values = _mm256_set1_epi32(v); }
  template <int64_t mask>
  static Vec256<int32_t> blend(Vec256<int32_t> a, Vec256<int32_t> b) {
    return _mm256_blend_epi32(a, b, mask);
  }
  static Vec256<int32_t>
  set(Vec256<int32_t> a, Vec256<int32_t> b, int32_t count = size) {
    switch (count) {
      case 0:
        return a;
      case 1:
        return blend<1>(a, b);
      case 2:
        return blend<3>(a, b);
      case 3:
        return blend<7>(a, b);
      case 4:
        return blend<15>(a, b);
      case 5:
        return blend<31>(a, b);
      case 6:
        return blend<63>(a, b);
      case 7:
        return blend<127>(a, b);
    }
    return b;
  }
  static Vec256<int32_t> load(
      const void* ptr,
      int64_t count = size,
      int64_t stride = 1) {
    if (count == size && stride == 1)
      return _mm256_loadu_si256(reinterpret_cast<const __m256i*>(ptr));
    __at_align32__ int32_t tmp_values[size];
    if (stride == 1) {
      std::memcpy(tmp_values, ptr, count * sizeof(int32_t));
    } else {
      for (int64_t i = 0; i < count; i++) {
        tmp_values[i] = reinterpret_cast<const int32_t*>(ptr)[i * stride];
      }
    }
    return load(tmp_values);
  }
  void store(void* ptr, int64_t count = size, int64_t stride = 1) const {
    if (count == size && stride == 1) {
      _mm256_storeu_si256(reinterpret_cast<__m256i*>(ptr), values);
    } else {
      __at_align32__ int32_t tmp_values[size];
      _mm256_storeu_si256(reinterpret_cast<__m256i*>(tmp_values), values);
      if (stride == 1) {
        std::memcpy(ptr, tmp_values, count * sizeof(int32_t));
      } else {
        for (int64_t i = 0; i < count; i++) {
          reinterpret_cast<int32_t*>(ptr)[i * stride] = tmp_values[i];
        }
      }
    }
  }
  const int32_t& operator[](int idx) const  = delete;
  int32_t& operator[](int idx)  = delete;
};

template <>
Vec256<int32_t> inline map(int32_t (*f)(int32_t), Vec256<int32_t> x) = delete;

template <>
Vec256<int32_t> inline abs(Vec256<int32_t> x) {
  return _mm256_abs_epi32(x);
}

template <>
struct Vec256<int16_t> : public Vec256i {
public:
  static constexpr int size = 16;
  using Vec256i::Vec256i;
  Vec256() {}
  Vec256(int16_t v) { values = _mm256_set1_epi16(v); }
  template <int64_t mask>
  static Vec256<int16_t> blend(Vec256<int16_t> a, Vec256<int16_t> b) {
    __at_align32__ int16_t tmp_values[size];
    a.store(tmp_values);
    if (mask & 0x01)
      tmp_values[0] = _mm256_extract_epi16(b, 0);
    if (mask & 0x02)
      tmp_values[1] = _mm256_extract_epi16(b, 1);
    if (mask & 0x04)
      tmp_values[2] = _mm256_extract_epi16(b, 2);
    if (mask & 0x08)
      tmp_values[3] = _mm256_extract_epi16(b, 3);
    if (mask & 0x10)
      tmp_values[4] = _mm256_extract_epi16(b, 4);
    if (mask & 0x20)
      tmp_values[5] = _mm256_extract_epi16(b, 5);
    if (mask & 0x40)
      tmp_values[6] = _mm256_extract_epi16(b, 6);
    if (mask & 0x80)
      tmp_values[7] = _mm256_extract_epi16(b, 7);
    if (mask & 0x100)
      tmp_values[8] = _mm256_extract_epi16(b, 8);
    if (mask & 0x200)
      tmp_values[9] = _mm256_extract_epi16(b, 9);
    if (mask & 0x400)
      tmp_values[10] = _mm256_extract_epi16(b, 10);
    if (mask & 0x800)
      tmp_values[11] = _mm256_extract_epi16(b, 11);
    if (mask & 0x1000)
      tmp_values[12] = _mm256_extract_epi16(b, 12);
    if (mask & 0x2000)
      tmp_values[13] = _mm256_extract_epi16(b, 13);
    if (mask & 0x4000)
      tmp_values[14] = _mm256_extract_epi16(b, 14);
    if (mask & 0x8000)
      tmp_values[15] = _mm256_extract_epi16(b, 15);
    return load(tmp_values);
  }
  static Vec256<int16_t>
  set(Vec256<int16_t> a, Vec256<int16_t> b, int16_t count = size) {
    switch (count) {
      case 0:
        return a;
      case 1:
        return blend<1>(a, b);
      case 2:
        return blend<3>(a, b);
      case 3:
        return blend<7>(a, b);
      case 4:
        return blend<15>(a, b);
      case 5:
        return blend<31>(a, b);
      case 6:
        return blend<63>(a, b);
      case 7:
        return blend<127>(a, b);
      case 8:
        return blend<255>(a, b);
      case 9:
        return blend<511>(a, b);
      case 10:
        return blend<1023>(a, b);
      case 11:
        return blend<2047>(a, b);
      case 12:
        return blend<4095>(a, b);
      case 13:
        return blend<8191>(a, b);
      case 14:
        return blend<16383>(a, b);
      case 15:
        return blend<32767>(a, b);
    }
    return b;
  }
  static Vec256<int16_t> load(
      const void* ptr,
      int64_t count = size,
      int64_t stride = 1) {
    if (count == size && stride == 1)
      return _mm256_loadu_si256(reinterpret_cast<const __m256i*>(ptr));
    __at_align32__ int16_t tmp_values[size];
    if (stride == 1) {
      std::memcpy(tmp_values, ptr, count * sizeof(int16_t));
    } else {
      for (int64_t i = 0; i < count; i++) {
        tmp_values[i] = reinterpret_cast<const int16_t*>(ptr)[i * stride];
      }
    }
    return load(tmp_values);
  }
  void store(void* ptr, int64_t count = size, int64_t stride = 1) const {
    if (count == size && stride == 1) {
      _mm256_storeu_si256(reinterpret_cast<__m256i*>(ptr), values);
    } else {
      __at_align32__ int16_t tmp_values[size];
      _mm256_storeu_si256(reinterpret_cast<__m256i*>(tmp_values), values);
      if (stride == 1) {
        std::memcpy(ptr, tmp_values, count * sizeof(int16_t));
      } else {
        for (int64_t i = 0; i < count; i++) {
          reinterpret_cast<int16_t*>(ptr)[i * stride] = tmp_values[i];
        }
      }
    }
  }
  const int16_t& operator[](int idx) const  = delete;
  int16_t& operator[](int idx)  = delete;
};

template <>
Vec256<int16_t> inline map(int16_t (*f)(int16_t), Vec256<int16_t> x) = delete;

template <>
Vec256<int16_t> inline abs(Vec256<int16_t> x) {
  return _mm256_abs_epi16(x);
}

template <>
Vec256<int64_t> inline operator+(const Vec256<int64_t>& a, const Vec256<int64_t>& b) {
  return _mm256_add_epi64(a, b);
}

template <>
Vec256<int32_t> inline operator+(const Vec256<int32_t>& a, const Vec256<int32_t>& b) {
  return _mm256_add_epi32(a, b);
}

template <>
Vec256<int16_t> inline operator+(const Vec256<int16_t>& a, const Vec256<int16_t>& b) {
  return _mm256_add_epi16(a, b);
}

template <>
Vec256<int64_t> inline operator-(const Vec256<int64_t>& a, const Vec256<int64_t>& b) {
  return _mm256_sub_epi64(a, b);
}

template <>
Vec256<int32_t> inline operator-(const Vec256<int32_t>& a, const Vec256<int32_t>& b) {
  return _mm256_sub_epi32(a, b);
}

template <>
Vec256<int16_t> inline operator-(const Vec256<int16_t>& a, const Vec256<int16_t>& b) {
  return _mm256_sub_epi16(a, b);
}

// AVX2 has no intrinsic for int64_t multiply so it needs to be emulated
// This could be implemented more efficiently using epi32 instructions
// This is also technically avx compatible, but then we'll need AVX
// code for add as well.
template <>
Vec256<int64_t> inline operator*(const Vec256<int64_t>& a, const Vec256<int64_t>& b) {
  __at_align32__ int64_t a_values[Vec256<int64_t>::size];
  __at_align32__ int64_t b_values[Vec256<int64_t>::size];
  a.store(a_values);
  b.store(b_values);
  int64_t c0 = a_values[0] * b_values[0];
  int64_t c1 = a_values[1] * b_values[1];
  int64_t c2 = a_values[2] * b_values[2];
  int64_t c3 = a_values[3] * b_values[3];
  return _mm256_set_epi64x(c3, c2, c1, c0);
}

template <>
Vec256<int32_t> inline operator*(const Vec256<int32_t>& a, const Vec256<int32_t>& b) {
  return _mm256_mullo_epi32(a, b);
}

template <>
Vec256<int16_t> inline operator*(const Vec256<int16_t>& a, const Vec256<int16_t>& b) {
  return _mm256_mullo_epi16(a, b);
}

template <>
Vec256<int64_t> inline max(const Vec256<int64_t>& a, const Vec256<int64_t>& b) {
  Vec256<int64_t> mask = _mm256_cmpgt_epi64(b, a);
  return _mm256_blendv_epi8(a, b, mask);
}

template <>
Vec256<int32_t> inline max(const Vec256<int32_t>& a, const Vec256<int32_t>& b) {
  return _mm256_max_epi32(a, b);
}

template <>
Vec256<int16_t> inline max(const Vec256<int16_t>& a, const Vec256<int16_t>& b) {
  return _mm256_max_epi16(a, b);
}

template <>
Vec256<int64_t> inline min(const Vec256<int64_t>& a, const Vec256<int64_t>& b) {
  Vec256<int64_t> mask = _mm256_cmpgt_epi64(a, b);
  return _mm256_blendv_epi8(a, b, mask);
}

template <>
Vec256<int32_t> inline min(const Vec256<int32_t>& a, const Vec256<int32_t>& b) {
  return _mm256_min_epi32(a, b);
}

template <>
Vec256<int16_t> inline min(const Vec256<int16_t>& a, const Vec256<int16_t>& b) {
  return _mm256_min_epi16(a, b);
}

#endif

}}}
