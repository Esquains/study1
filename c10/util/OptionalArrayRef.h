// Optimization for ArrayRef<T>. We take advantage of an unused bit pattern in
// ArrayRef (inspired by Arthur O'Dwyer's tombstone_traits -- see
// https://youtu.be/MWBfmmg8-Yo?t=2466) to keep the size of
// c10::optional::ArrayRef<T> down to 16 bytes, which allows it to be passed to
// functions in registers instead of getting passed in memory per item 5c of the
// classification algorithm in section 3.2.3 of the System V ABI document
// (https://www.uclibc.org/docs/psABI-x86_64.pdf).
//
// c10::optional<ArrayRef<T>> as well as std::optional<ArrayRef<T>> have a
// problem where the implicit converting constructor will cause the underlying
// ArrayRef<T> to store a dangling pointer. OptionalArrayRef<T> prevents this.
// See https://github.com/pytorch/pytorch/issues/63645 for more on this.

#pragma once

#include <c10/util/ArrayRef.h>
#include <c10/util/Optional.h>

#include <initializer_list>
#include <iostream>
#include <vector>

namespace c10 {

/**
 * @brief Class template for optional ArrayRef<T> values.
 *
 * @tparam T type of the ArrayRef elements.
 */
template <typename T>
class OptionalArrayRef final {
 public:
  using value_type = ArrayRef<T>;

  // Constructors

  constexpr OptionalArrayRef() noexcept {}

  constexpr OptionalArrayRef(nullopt_t) noexcept {}

  OptionalArrayRef(const OptionalArrayRef& other) = default;

  OptionalArrayRef(OptionalArrayRef&& other) = default;

  constexpr OptionalArrayRef(const optional<ArrayRef<T>>& other) noexcept {
    if (other.has_value()) {
      if (initialized()) {
        storage.value = other.value();
      } else {
        ::new (&storage.value) ArrayRef<T>(other.value());
      }
    }
  }

  constexpr OptionalArrayRef(optional<ArrayRef<T>>&& other) noexcept {
    if (other.has_value()) {
      if (initialized()) {
        storage.value = std::move(other.value());
      } else {
        ::new (&storage.value) ArrayRef<T>(std::move(other.value()));
      }
      other.reset();
    }
  }

  constexpr OptionalArrayRef(const T& value) noexcept
      : storage(in_place, value) {}

  template <
      typename U = ArrayRef<T>,
      std::enable_if_t<
          !std::is_same<std::decay_t<U>, OptionalArrayRef>::value &&
              !std::is_same<std::decay_t<U>, in_place_t>::value &&
              std::is_constructible<ArrayRef<T>, U&&>::value &&
              std::is_convertible<U&&, ArrayRef<T>>::value &&
              !std::is_convertible<U&&, T>::value,
          bool> = false>
  constexpr OptionalArrayRef(U&& value) noexcept(
      std::is_nothrow_constructible<ArrayRef<T>, U&&>::value)
      : storage(in_place, std::forward<U>(value)) {}

  template <
      typename U = ArrayRef<T>,
      std::enable_if_t<
          !std::is_same<std::decay_t<U>, OptionalArrayRef>::value &&
              !std::is_same<std::decay_t<U>, in_place_t>::value &&
              std::is_constructible<ArrayRef<T>, U&&>::value &&
              !std::is_convertible<U&&, ArrayRef<T>>::value,
          bool> = false>
  constexpr explicit OptionalArrayRef(U&& value) noexcept(
      std::is_nothrow_constructible<ArrayRef<T>, U&&>::value)
      : storage(in_place, std::forward<U>(value)) {}

  template <
      typename... Args,
      std::enable_if_t<
          std::is_constructible<ArrayRef<T>, Args&&...>::value,
          bool> = false>
  constexpr explicit OptionalArrayRef(in_place_t, Args&&... args) noexcept(
      std::is_nothrow_constructible<ArrayRef<T>, Args&&...>::value)
      : storage(in_place, std::forward<Args>(args)...) {}

  template <
      typename U,
      typename... Args,
      std::enable_if_t<
          std::is_constructible<
              ArrayRef<T>,
              std::initializer_list<U>&,
              Args&&...>::value,
          bool> = false>
  constexpr explicit OptionalArrayRef(
      in_place_t,
      std::initializer_list<U> il,
      Args&&... args) noexcept(std::
                                   is_nothrow_constructible<
                                       ArrayRef<T>,
                                       std::initializer_list<U>&,
                                       Args&&...>::value)
      : storage(il, std::forward<Args>(args)...) {}

  // Destructor

  ~OptionalArrayRef() = default;

  // Assignment

  constexpr OptionalArrayRef& operator=(nullopt_t) noexcept {
    reset();
    return *this;
  }

  OptionalArrayRef& operator=(const OptionalArrayRef& other) = default;

  OptionalArrayRef& operator=(OptionalArrayRef&& other) = default;

  constexpr OptionalArrayRef& operator=(
      const optional<ArrayRef<T>>& other) noexcept {
    if (other.has_value()) {
      if (initialized()) {
        storage.value = other.value();
      } else {
        ::new (&storage.value) ArrayRef<T>(other.value());
      }
    } else {
      reset();
    }
    return *this;
  }

  constexpr OptionalArrayRef& operator=(
      optional<ArrayRef<T>>&& other) noexcept {
    if (other.has_value()) {
      if (initialized()) {
        storage.value = std::move(other.value());
      } else {
        ::new (&storage.value) ArrayRef<T>(std::move(other.value()));
      }
      other.reset();
    } else {
      reset();
    }
    return *this;
  }

  template <typename U = ArrayRef<T>>
  constexpr std::enable_if_t<
      !std::is_same<std::decay_t<U>, OptionalArrayRef>::value &&
          std::is_constructible<ArrayRef<T>, U&&>::value &&
          std::is_assignable<ArrayRef<T>&, U&&>::value,
      OptionalArrayRef&>
  operator=(U&& value) noexcept(
      std::is_nothrow_constructible<ArrayRef<T>, U&&>::value&&
          std::is_nothrow_assignable<ArrayRef<T>&, U&&>::value) {
    if (initialized()) {
      storage.value = std::forward<U>(value);
    } else {
      ::new (&storage.value) ArrayRef<T>(std::forward<U>(value));
    }
    return *this;
  }

  // Observers

  constexpr ArrayRef<T>* operator->() noexcept {
    return &storage.value;
  }

  constexpr const ArrayRef<T>* operator->() const noexcept {
    return &storage.value;
  }

  constexpr ArrayRef<T>& operator*() & noexcept {
    return storage.value;
  }

  constexpr const ArrayRef<T>& operator*() const& noexcept {
    return storage.value;
  }

  constexpr ArrayRef<T>&& operator*() && noexcept {
    return std::move(storage.value);
  }

  constexpr const ArrayRef<T>&& operator*() const&& noexcept {
    return std::move(storage.value);
  }

  constexpr explicit operator bool() const noexcept {
    return initialized();
  }

  constexpr bool has_value() const noexcept {
    return initialized();
  }

  constexpr ArrayRef<T>& value() & {
    if (!initialized()) {
      throw c10::bad_optional_access("");
    }
    return storage.value;
  }

  constexpr const ArrayRef<T>& value() const& {
    if (!initialized()) {
      throw c10::bad_optional_access("");
    }
    return storage.value;
  }

  constexpr ArrayRef<T>&& value() && {
    if (!initialized()) {
      throw c10::bad_optional_access("");
    }
    return std::move(storage.value);
  }

  constexpr const ArrayRef<T>&& value() const&& {
    if (!initialized()) {
      throw c10::bad_optional_access("");
    }
    return std::move(storage.value);
  }

  template <typename U>
  constexpr std::
      enable_if_t<std::is_convertible<U&&, ArrayRef<T>>::value, ArrayRef<T>>
      value_or(U&& default_value) const& {
    return initialized()
        ? storage.value
        : static_cast<ArrayRef<T>>(std::forward<U>(default_value));
  }

  template <typename U>
  constexpr std::
      enable_if_t<std::is_convertible<U&&, ArrayRef<T>>::value, ArrayRef<T>>
      value_or(U&& default_value) && {
    return initialized()
        ? std::move(storage.value)
        : static_cast<ArrayRef<T>>(std::forward<U>(default_value));
  }

  // Modifiers

  constexpr void swap(OptionalArrayRef& other) noexcept {
    if (initialized() && other.initialized()) {
      std::swap(storage.value, other.storage.value);
    } else if (initialized()) {
      ::new (&other.storage.value) ArrayRef<T>(std::move(storage.value));
      reset();
    } else if (other.initialized()) {
      ::new (&storage.value) ArrayRef<T>(std::move(other.storage.value));
      other.reset();
    }
  }

  constexpr void reset() noexcept {
    if (initialized()) {
      ::new (&storage.empty) Empty();
    }
  }

  template <typename... Args>
  constexpr std::enable_if_t<
      std::is_constructible<ArrayRef<T>, Args&&...>::value,
      ArrayRef<T>&>
  emplace(Args&&... args) noexcept(
      std::is_nothrow_constructible<ArrayRef<T>, Args&&...>::value) {
    ::new (&storage.value) ArrayRef<T>(std::forward<Args>(args)...);
    return storage.value;
  }

  template <typename U, typename... Args>
  constexpr std::enable_if_t<
      std::is_constructible<ArrayRef<T>, std::initializer_list<U>&, Args&&...>::
          value,
      ArrayRef<T>&>
  emplace(std::initializer_list<U> il, Args&&... args) noexcept(
      std::is_nothrow_constructible<
          ArrayRef<T>,
          std::initializer_list<U>&,
          Args&&...>::value) {
    ::new (&storage.value) ArrayRef<T>(il, std::forward<Args>(args)...);
    return storage.value;
  }

 private:
  struct Empty {
    // ArrayRef has the invariant that if Data is nullptr then
    // Length must be zero, so this is an unused bit pattern.
    const T* data{nullptr};
    typename ArrayRef<T>::size_type size{1};
  };

  union Storage {
    constexpr Storage() noexcept : empty() {}

    template <typename... Args>
    constexpr explicit Storage(in_place_t, Args&&... args)
        : value(std::forward<Args>(args)...) {}

    template <typename U, typename... Args>
    constexpr explicit Storage(std::initializer_list<U> il, Args&&... args)
        : value(il, std::forward<Args>(args)...) {}

    Empty empty;
    ArrayRef<T> value;
  };

  Storage storage;

  constexpr bool initialized() const noexcept {
    return storage.empty.data != nullptr || storage.empty.size == 0;
  }
};

using OptionalIntArrayRef = OptionalArrayRef<int64_t>;

inline bool operator==(
    const OptionalIntArrayRef& a1,
    const IntArrayRef& other) {
  if (!a1.has_value()) {
    return false;
  }
  return a1.value() == other;
}

inline bool operator==(
    const c10::IntArrayRef& a1,
    const c10::OptionalIntArrayRef& a2) {
  return a2 == a1;
}

} // namespace c10
