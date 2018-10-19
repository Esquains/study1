#pragma once

#include <atomic>
#include <memory>

#include <ATen/core/Backend.h>
#include <ATen/core/LegacyTypeDispatch.h>
#include <ATen/core/Storage.h>
#include <ATen/core/TensorOptions.h>
#include <ATen/core/TensorTypeId.h>
#include <ATen/core/TensorTypeIdRegistration.h>
#include <ATen/core/context_base.h>

#include <c10/util/Exception.h>
#include "c10/util/Optional.h"

#include "c10/util/Flags.h"

#include "caffe2/core/allocator.h"
#include "caffe2/core/common.h"
#include "caffe2/core/logging.h"

// A global boolean variable to control whether we free memory when a Tensor
// is shrinked to a smaller size. As a result, a Tensor is always going to
// keep the memory allocated for its maximum capacity reshaped to so far.
//
// This parameter is respected "upper-case" methods which call Resize()
// (e.g., CopyFrom, ResizeLike); it is NOT respected by Tensor::resize_
// or ShrinkTo, both of which guarantee to never to free memory.
C10_DECLARE_bool(caffe2_keep_on_shrink);

// Since we can have high variance in blob memory allocated across different
// inputs in the same run, we will shrink the blob only if the memory gain
// is larger than this flag in bytes.  This only applies to functions which
// respect caffe2_keep_on_shrink.
C10_DECLARE_int64(caffe2_max_keep_on_shrink_memory);

namespace caffe2 {

// Defined by protobuf
class DeviceOption;

}

namespace at {
class Scalar;
struct Type;
struct Storage;
class Tensor;

/**
 * A utility function to convert vector<int> to vector<int64_t>.
 */
inline std::vector<int64_t> ToVectorint64_t(ArrayRef<int> src) {
  return std::vector<int64_t>(src.begin(), src.end());
}

/**
 * Return product of all dimensions starting from k
 */
inline int64_t size_from_dim_(int k, IntList dims) {
  int64_t r = 1;
  for (size_t i = k; i < dims.size(); ++i) {
    r *= dims[i];
  }
  return r;
}

// Product of all dims up to k (not including dims[k])
inline int64_t size_to_dim_(int k, IntList dims) {
  AT_ASSERT((unsigned)k <= dims.size());
  int64_t r = 1;
  for (int i = 0; i < k; ++i) {
    r *= dims[i];
  }
  return r;
}

// Product of all dims between k and l (not including dims[k] and dims[l])
inline int64_t size_between_dim_(int k, int l, IntList dims) {
  AT_ASSERT((unsigned)l < dims.size());
  int64_t r = 1;
  if (k < l) {
    for (int i = k + 1; i < l; ++i) {
      r *= dims[i];
    }
  } else {
    for (int i = l + 1; i < k; ++i) {
      r *= dims[i];
    }
  }
  return r;
}

// Wrap around axis_index if it is negative, s.t., -1 is the last dim
inline int canonical_axis_index_(int axis_index, int ndims) {
  AT_ASSERT(axis_index >= -ndims);
  AT_ASSERT(axis_index < ndims);
  if (axis_index < 0) {
    return axis_index + ndims;
  }
  return axis_index;
}

using PlacementDtor = void (*)(void*, size_t);

/*
 * A Context that will call extra placement deleter during
 * deconstruction.
 *
 * Accept a already constructed DataPtr and store it as member
 * during destruction, we'll call extra deleter on the underlying
 * data pointer before the DataPtr is destructed.
 * `data_ptr_` owns the memory.
 */
struct CAFFE2_API PlacementDeleteContext {
  at::DataPtr data_ptr_;
  PlacementDtor placement_dtor_;
  size_t size_;
  PlacementDeleteContext(
      at::DataPtr&& data_ptr,
      PlacementDtor placement_dtor,
      size_t size)
      : data_ptr_(std::move(data_ptr)),
        placement_dtor_(placement_dtor),
        size_(size) {}
  static at::DataPtr makeDataPtr(
      at::DataPtr&& data_ptr,
      PlacementDtor placement_dtor,
      size_t size,
      at::Device device);
  ~PlacementDeleteContext() {
    placement_dtor_(data_ptr_.get(), size_);
    // original memory will be freed when data_ptr_ is destructed
  }
};

namespace detail {
  // This is intended to be a centralized location by which we can determine
  // what an appropriate TensorTypeId for a tensor is.
  //
  // This takes a TensorOptions, rather than just a DeviceType and Layout, because
  // we reserve the right to change dispatch based on *any* aspect of
  // TensorOptions.  WARNING: If you do this, you need to fix the calls
  // to computeTensorTypeId in caffe2/tensor.h
  inline TensorTypeId computeTensorTypeId(TensorOptions options) {
    switch (options.layout()) {
      case Layout::Strided:
        switch (options.device().type()) {
          case DeviceType::CPU:
            return CPUTensorId();
          case DeviceType::CUDA:
            return CUDATensorId();
          case DeviceType::MKLDNN:
            return MKLDNNTensorId();
          case DeviceType::OPENGL:
            return OpenGLTensorId();
          case DeviceType::OPENCL:
            return OpenCLTensorId();
          case DeviceType::IDEEP:
            return IDEEPTensorId();
          case DeviceType::HIP:
            return HIPTensorId();
          default:
            AT_ERROR("Unsupported device type for dense layout: ", options.device().type());
        }
      case Layout::Sparse:
        switch (options.device().type()) {
          case DeviceType::CPU:
            return SparseCPUTensorId();
          case DeviceType::CUDA:
            return SparseCUDATensorId();
          default:
            AT_ERROR("Unsupported device type for sparse layout: ", options.device().type());
        }
      default:
        AT_ERROR("Unsupported layout: ", options.layout());
    }
  }
} // namespace detail

/**
 * The low-level representation of a tensor, which contains a pointer
 * to a storage (which contains the actual data) and metadata (e.g., sizes and
 * strides) describing this particular view of the data as a tensor.
 *
 * Some basic characteristics about our in-memory representation of
 * tensors:
 *
 *  - It contains a pointer to a storage struct (Storage/StorageImpl)
 *    which contains the pointer to the actual data and records the
 *    data type and device of the view.  This allows multiple tensors
 *    to alias the same underlying data, which allows to efficiently
 *    implement differing *views* on a tensor.
 *
 *  - The tensor struct itself records view-specific metadata about
 *    the tensor, e.g., sizes, strides and offset into storage.
 *    Each view of a storage can have a different size or offset.
 *
 *  - This class is intrusively refcounted.  It is refcounted so that
 *    we can support prompt deallocation of large tensors; it is
 *    intrusively refcounted so that we can still perform reference
 *    counted operations on raw pointers, which is often more convenient
 *    when passing tensors across language boundaries.
 *
 *  - For backwards-compatibility reasons, a tensor may be in an
 *    uninitialized state.  A tensor may be uninitialized in the following
 *    two ways:
 *
 *      - A tensor may be DTYPE UNINITIALIZED.  A tensor of this
 *        form has an uninitialized dtype.  This situation most
 *        frequently arises when a user writes Tensor x(CPU).  The dtype and
 *        is subsequently initialized when mutable_data<T>() is
 *        invoked for the first time.
 *
 *      - A tensor may be STORAGE UNINITIALIZED.  A tensor of this form
 *        has non-zero size, but has a storage with a null data pointer.
 *        This situation most frequently arises when a user calls
 *        Resize() or FreeMemory().  This is because Caffe2 historically
 *        does lazy allocation: allocation of data doesn't occur until
 *        mutable_data<T>() is invoked.  A tensor with zero size is
 *        always storage initialized, because no allocation is necessary
 *        in this case.
 *
 *    All combinations of these two uninitialized states are possible.
 *    Consider the following transcript in idiomatic Caffe2 API:
 *
 *      Tensor x(CPU); // x is storage-initialized, dtype-UNINITIALIZED
 *      x.Resize(4); // x is storage-UNINITIALIZED, dtype-UNINITIALIZED
 *      x.mutable_data<float>(); // x is storage-initialized, dtype-initialized
 *      x.FreeMemory(); // x is storage-UNINITIALIZED, dtype-initialized.
 *
 *    All other fields on tensor are always initialized.  In particular,
 *    size is always valid. (Historically, a tensor declared as Tensor x(CPU)
 *    also had uninitialized size, encoded as numel == -1, but we have now
 *    decided to default to zero size, resulting in numel == 0).
 *
 *    Uninitialized storages MUST be uniquely owned, to keep our model
 *    simple.  Thus, we will reject operations which could cause an
 *    uninitialized storage to become shared (or a shared storage to
 *    become uninitialized, e.g., from FreeMemory).
 *
 *    In practice, tensors which are storage-UNINITIALIZED and
 *    dtype-UNINITIALIZED are *extremely* ephemeral: essentially,
 *    after you do a Resize(), you basically always call mutable_data()
 *    immediately afterwards.  Most functions are not designed to
 *    work if given a storage-UNINITIALIZED, dtype-UNINITIALIZED tensor.
 *
 *    We intend to eliminate all uninitialized states, so that every
 *    tensor is fully initialized in all fields.  Please do not write new code
 *    that depends on these uninitialized states.
 */
struct CAFFE2_API TensorImpl : public c10::intrusive_ptr_target {
  TensorImpl() = delete;
  TensorImpl(TensorTypeId type_id, const caffe2::TypeMeta& data_type, Allocator *allocator, bool is_variable);
  TensorImpl(Storage&& storage, TensorTypeId type_id, bool is_variable);

 private:
  TensorImpl(Storage&& storage, TensorTypeId type_id, const caffe2::TypeMeta& data_type, bool is_variable);

 public:
  TensorImpl(const TensorImpl&) = default;
  TensorImpl& operator=(const TensorImpl&) = default;
  TensorImpl(TensorImpl&&) = default;
  TensorImpl& operator=(TensorImpl&&) = default;

  virtual void release_resources() override;

  // TODO: Ideally, type_id() would be the *only* key we need to consult
  // to do a dispatch, instead of having to grovel through three different
  // variables.  Here's what's standing in the way:
  //
  //  - To eliminate ScalarType, we have to allocate a TensorTypeId for
  //    each ScalarType+Backend combination, and then set it appropriately
  //    when we initially allocate a TensorImpl.
  //
  //  - To eliminate is_variable, we have to allocate two classes of
  //    TensorTypeId: ones that are variables, and ones that are not.
  //    We may not want to eliminate this in the short term, because
  //    hard-coding variable status into type_id() makes it more difficult
  //    to do the "thread-local no_grad" trick (where we process Variables
  //    "as if" they were non-Variables by setting a thread local variable.)
  //
  Type & type() const {
    // NB: It's valid to use getTypeRaw here, because the TensorImpl
    // could not have been created without initializing the Type first.
    // TODO: This is not actually true via the Caffe2 codepath!  Make
    // it so.
    return *globalLegacyTypeDispatch().getTypeRaw(tensorTypeIdToBackend(type_id()), dataTypeToScalarType(dtype().id()), is_variable());
  }

  TensorTypeId type_id() const { return type_id_; }
  virtual IntList sizes() const;
  virtual IntList strides() const;
  virtual int64_t dim() const;
  virtual const Storage& storage() const;
  friend struct Type;

  /**
   * The number of elements in a tensor.
   *
   * WARNING: Previously, if you were using the Caffe2 API, you could
   * test numel() == -1 to see if a tensor was uninitialized.  This
   * is no longer true; numel always accurately reports the product
   * of sizes of a tensor.
   */
  virtual int64_t numel() const {
#ifdef DEBUG
    AT_ASSERT(compute_numel() == numel_);
#endif
    return numel_;
  }

  virtual bool is_contiguous() const {
#ifdef DEBUG
    AT_ASSERT(compute_contiguous() == is_contiguous_);
#endif
    return is_contiguous_;
  }

  // this is called by the generated wrapper code when there are conditions
  // when this output tensor should be zero dimensional. e.g. when all inputs
  // to a function 'add' were zero dimensional, then condition_when_zero_dim == true.
  // we also prevent this from getting marked as a zero dim tensor if it is not
  // the right shape afterall.
  virtual TensorImpl* maybe_zero_dim(bool condition_when_zero_dim);

  // True if a tensor was auto-wrapped from a C++ or Python number.
  // Wrapped numbers do not participate in the result type computation for
  // mixed-type operations if there are any Tensors that are not wrapped
  // numbers. Otherwise, they behave like their non-wrapped equivalents.
  // See [Result type computation] in TensorIterator.h.
  bool is_wrapped_number() const {
    AT_ASSERT(!is_variable());
    return is_wrapped_number_;
  }
  void set_wrapped_number(bool value) {
    AT_ASSERT(!is_variable());
    AT_ASSERT(dim() == 0);
    is_wrapped_number_ = value;
  }

  // ~~~~~ Autograd API ~~~~~
  // Some methods below are defined in TensorImpl.cpp because Tensor is an
  // incomplete type.

  virtual void set_requires_grad(bool requires_grad) {
    AT_ERROR("set_requires_grad is not implemented for Tensor");
  }
  virtual bool requires_grad() const {
    AT_ERROR("requires_grad is not implemented for Tensor");
  }

  virtual Tensor& grad();
  virtual const Tensor& grad() const;

  template <typename T>
  inline T * data() const {
    AT_ASSERT(!is_variable());
    AT_ASSERTM(
        storage_initialized(),
        "The tensor has a non-zero number of elements, but its data is not allocated yet. "
        "Caffe2 uses a lazy allocation, so you will need to call "
        "mutable_data() or raw_mutable_data() to actually allocate memory.");
    AT_ASSERTM(
        storage_.IsType<T>(),
        "Tensor type mismatch, caller expects elements to be ",
        caffe2::TypeMeta::TypeName<T>(),
        ", while tensor contains ",
        data_type_.name(),
        ". ");
    // We managed the type check ourselves
    return storage_.unsafe_data<T>() + storage_offset_;
  }

  inline void* data() const {
    AT_ASSERT(!is_variable());
    AT_ASSERT(storage_initialized());
    AT_ASSERT(dtype_initialized());
    return static_cast<void*>(
        static_cast<char*>(storage_.data()) +
        data_type_.itemsize() * storage_offset_);
  }

  template <typename T>
  inline T * unsafe_data() const {
    AT_ASSERT(!is_variable());
    return storage_.unsafe_data<T>() + storage_offset_;
  }

  const caffe2::TypeMeta& dtype() const {
    return data_type_;
  }
  size_t itemsize() const {
    AT_ASSERT(dtype_initialized());
    return data_type_.itemsize();
  }

  virtual int64_t storage_offset() const {
    return storage_offset_;
  }

  // represents that numel() == 0.
  inline bool is_empty() const {
    return numel() == 0;
  }

  virtual void resize_dim(int64_t ndim) {
    // NB: This is *truly* a resize; calling code (e.g., squeeze)
    // assumes that old values are preserved
    auto old_dim = sizes_.size();
    sizes_.resize(ndim);
    if (old_dim != sizes_.size()) {
      auto new_strides = c10::guts::make_unique<int64_t[]>(ndim);
      for (size_t i = 0; i < std::min(old_dim, static_cast<size_t>(ndim)); i++) {
        new_strides[i] = strides_[i];
      }
      for (size_t i = old_dim; i < static_cast<size_t>(ndim); i++) {
        // If ndim < old_dim, this loop never executes
        new_strides[i] = 0;
      }
      strides_ = std::move(new_strides);
    }
    refresh_numel();
    refresh_contiguous();
  }

  virtual void set_size(int64_t dim, int64_t new_size) {
    sizes_.at(dim) = new_size;
    refresh_numel();
    refresh_contiguous();
  }

  virtual void set_stride(int64_t dim, int64_t new_stride) {
    strides_[dim] = new_stride;
    refresh_numel();
    refresh_contiguous();
  }

  virtual void set_storage_offset(int64_t storage_offset) {
    storage_offset_ = storage_offset;
    refresh_numel();
    refresh_contiguous();
  }

  // Like set_sizes_and_strides but assumes contiguous strides.`
  //
  // WARNING: This function does not check if the requested
  // sizes/strides are in bounds for the storage that is allocated;
  // this is the responsibility of the caller
  void set_sizes_contiguous(at::IntList new_size) {
    AT_ASSERT(!is_variable());
    auto old_dim = sizes_.size();
    auto new_dim = new_size.size();
    sizes_ = new_size.vec();

    // TODO(rzou): replace with update_to_contiguous_strides
    // in #12845 when that lands.
    {
      if (old_dim != new_dim) {
        strides_.reset(new int64_t[sizes_.size()]);
      }
      if (new_dim > 0) {
        for (size_t d = new_dim - 1; ; d--) {
          if (d == new_dim - 1) {
            strides_[d] = 1;
          } else {
            // Keep stride monotonically increasing to match NumPy.
            strides_[d] = std::max<int64_t>(sizes_[d+1], 1) * strides_[d+1];
          }
          if (d == 0) break;
        }
      }
      is_contiguous_ = true;
    }

    refresh_numel();
  }

  // WARNING: This function does not check if the requested
  // sizes/strides are in bounds for the storage that is allocated;
  // this is the responsibility of the caller
  void set_sizes_and_strides(at::IntList new_size, at::IntList new_stride) {
    AT_ASSERT(!is_variable());
    AT_CHECK(
        new_size.size() == new_stride.size(),
        "dimensionality of sizes (",
        new_size.size(),
        ") must match dimensionality of strides (",
        new_stride.size(),
        ")");
    auto old_dim = sizes_.size();
    sizes_ = new_size.vec();
    if (old_dim != sizes_.size()) {
      strides_.reset(new int64_t[sizes_.size()]);
    }
    for (size_t i = 0; i < sizes_.size(); i++) {
      strides_[i] = new_stride[i];
    }
    refresh_numel();
    refresh_contiguous();
  }

  virtual int64_t size(int64_t d) const;
  virtual int64_t stride(int64_t d) const;

  bool is_variable() const { return is_variable_; };

 private:
  int64_t compute_numel() const {
    int64_t n = 1;
    for (auto s : sizes()) {
      n *= s;
    }
    return n;
  }
  bool compute_contiguous() const;

 protected:
  void refresh_numel() {
    AT_ASSERT(!is_variable());
    numel_ = compute_numel();
  }
  void refresh_contiguous() {
    AT_ASSERT(!is_variable());
    is_contiguous_ = compute_contiguous();
  }

 public:

  at::DeviceType device_type() const {
    AT_ASSERT(!is_variable());
    return storage_.device_type();
  }

  at::Device GetDevice() const {
    return storage_.device();
  }

  /**
   * @brief Copies the data from a source tensor, with a contex provided to
   * carry out the underlying memcpy operation.  This method respects
   * caffe2_keep_on_shrink.
   *
   * After CopyFrom, this function guarantees that the destination tensor will
   * have the same initialization state and dtype as src.  This function
   * preserves the DeviceType of the source tensor (so, e.g., if you allocate
   * a tensor on CPU and then CopyFrom a CUDA tensor, that will to a
   * CUDA-to-CPU transfer).
   */
  void CopyFrom(const TensorImpl& src, at::BaseContext* context = nullptr) {
    AT_ASSERT(!is_variable());
    AT_ASSERTM(
        src.is_contiguous(),
        "Right now only copy of contiguous source Tensor is supported.");

    if ((void*)&src == (void*)this) {
      return;
    }

    // Test if we need to allocate a new storage
    // Uninitialized storages are guaranteed to be uniquely owned,
    // so we don't need to swap in this case.
    if (storage_initialized()) {
      // If the dtype changed, we need to reallocate;
      // If the src storage is uninitialized, we need to reallocate
      // to preserve the unique storage invariant.
      if (data_type_ != src.dtype() || !src.storage_initialized()) {
        // NB: copy preserves device_type
        // This storage will get initialized by the mutable_data call below.
        storage_ = at::Storage(device_type(), src.dtype());
      }
    }
    data_type_ = src.dtype();
    Resize(src.sizes());

    if (src.storage_initialized() && numel() > 0) {

      // Only do an actual data copy if we actually have an initialized storage
      // to copy from (NB: we have !storage_initialized() at this point if
      // data_type_ != src.dtype() but src.storage_initialized(), since
      // we're waiting for the raw_mutable_data call to actually initialize
      // the storage)

      if (data_type_.copy()) {
        AT_ASSERTM(
            device_type() == ::at::DeviceType::CPU,
            "In CopyFrom source and dest tensors must both be CPU for meta copy, "
            "but dest tensor was ", device_type());
        AT_ASSERTM(
            src.device_type() == ::at::DeviceType::CPU,
            "In CopyFrom source and dest tensors must both be CPU for meta copy, "
            "but src tensor was ", src.device_type());
        data_type_.copy()(src.data(), raw_mutable_data(data_type_), numel());
      } else {
        // We'll need to use a non-CPU context to perform the copy if
        // one of the context is not CPU since only non-CPU context
        // knows how to copy between CPU and that context
        if (src.device_type() != ::at::DeviceType::CPU || device_type() == ::at::DeviceType::CPU) {
          if (!context) {
            CreateContext(src.GetDevice())
                ->CopyBytesToDevice(
                    numel() * itemsize(),
                    src.data(),
                    raw_mutable_data(data_type_),
                    device_type());
          } else {
            AT_ASSERTM(
                context->device_type() == src.device_type(),
                "Type for provided context does not match the type of source");
            context->CopyBytesToDevice(
                numel() * itemsize(), src.data(), raw_mutable_data(data_type_), device_type());
          }
        } else {
          // In case source context is CPU, and target context is non-CPU
          // We'll have to create a Context from target and perform the
          // copy using that context
          CreateContext(GetDevice())
              ->CopyBytesFromCPU(
                  numel() * itemsize(),
                  src.data(),
                  raw_mutable_data(data_type_));
        }
      }
    }
  }

  /**
   * @brief Extends the outer-most dimension of this tensor by num elements,
   * preserving the existing data.
   *
   * The underlying data may be reallocated in order to accommodate the new
   * elements, in which case this tensors' capacity is grown at a factor of
   * growthPct. This ensures that Extend runs on an amortized O(1) time
   * complexity.
   */
  void Extend(int64_t num, float growthPct, at::BaseContext* context) {
    AT_ASSERT(sizes_.size() >= 1u);
    AT_ASSERTM(num >= 0, "`num` must be non-negative for Extend");
    AT_ASSERTM(
        is_contiguous_,
        "Right now Extend is only supported for contiguous Tensor.");
    auto newDims = sizes_;
    newDims[0] += num;
    if (!storage_.data()) {
      Resize(newDims);
      return;
    }
    auto newNumel = std::accumulate(
        newDims.begin(),
        newDims.end(),
        static_cast<int64_t>(1),
        std::multiplies<int64_t>());
    if (newNumel * storage_.itemsize() <= storage_.capacity()) {
      sizes_ = newDims;
      numel_ = newNumel;
      return;
    }
    auto newCapacity = sizes_;
    newCapacity[0] = std::max<size_t>(
        newDims[0], std::ceil(sizes_[0] * (growthPct + 100) / 100));
    auto oldData = std::move(storage_.data_ptr());
    auto oldSize = numel_;
    auto oldDims = sizes_;
    Resize(newCapacity);
    auto* newData = raw_mutable_data(data_type_);
    AT_ASSERTM(
        context != nullptr, "Context must be provided to Extend the tensor");
    context->CopyItemsSameDevice(
        data_type_, oldSize, oldData.get(), newData);
    reserved_ = true;
    sizes_ = newDims;
    numel_ = newNumel;
  }

  /**
   * @brief Reserve space for the underlying tensor.
   *
   * This must be called after Resize(), since we only specify the first
   * dimension This does not copy over the old data to the newly allocated space
   */
  template <class T>
  void ReserveSpace(const T& outer_dim) {
    AT_ASSERTM(
        is_contiguous_,
        "Right now ReserveSpace is only supported for contiguous Tensor.");
    AT_ASSERTM(
        storage_.unique(), "Can't call ReserveSpace on shared storage.");
    auto newCapacity = sizes_;
    newCapacity[0] = outer_dim;
    auto newNumel = std::accumulate(
        newCapacity.begin(),
        newCapacity.end(),
        static_cast<int64_t>(1),
        std::multiplies<int64_t>());
    if (newNumel * storage_.itemsize() <= storage_.capacity()) {
      return;
    }
    // Old data is discarded
    storage_.data_ptr().clear();
    auto oldSize = numel_;
    auto oldDims = sizes_;
    Resize(newCapacity);
    // Allocate new memory but don't copy over the data
    raw_mutable_data(data_type_);
    sizes_ = oldDims;
    numel_ = oldSize;
    reserved_ = true;
  }

  /**
   * @brief Resizes a tensor.
   *
   * Resize takes in a vector of ints specifying the dimensions of the tensor.
   * You can pass in an empty vector to specify that it is a scalar (i.e.
   * containing one single item).
   *
   * The underlying storage may be deleted after calling Resize: if the new
   * shape leads to a different number of items in the tensor, the old memory
   * is deleted and new memory will be allocated next time you call
   * mutable_data(). However, if the shape is different but the total number of
   * items is the same, the underlying storage is kept.
   *
   * This method respects caffe2_keep_on_shrink.  Consult the internal logic
   * of this method to see exactly under what circumstances this flag matters.
   */
  template <typename... Ts>
  void Resize(Ts... dim_source) {
    bool size_changed = SetDims(dim_source...);
    if (size_changed) {
      // If needed, we will free the data. the next mutable_data() call
      // will create the data storage.
      bool reset_tensor = false;
      if (reserved_) {
        // If tensor is reserved then don't claim its memeory unless capacity()
        // is smaller than new size
        reset_tensor = storage_.capacity() < (storage_offset_ + numel_) * storage_.itemsize();
      } else {
        reset_tensor = storage_.capacity() <
                (storage_offset_ + numel_) * storage_.itemsize() ||
            !FLAGS_caffe2_keep_on_shrink ||
            storage_.capacity() -
                    (storage_offset_ + numel_) * storage_.itemsize() >
                static_cast<size_t>(FLAGS_caffe2_max_keep_on_shrink_memory);
      }

      if (reset_tensor && storage_initialized()) {
        FreeMemory();
      }
    }
  }

  /**
   * Resizes the tensor without touching underlying storage.
   * This requires the total size of the tensor to remains constant.
   */
  inline void Reshape(const std::vector<int64_t>& dims) {
    AT_ASSERTM(
        is_contiguous_,
        "Right now Reshape is only supported for contiguous Tensor.");
    int64_t new_size = 1;
    for (auto d : dims) {
      AT_ASSERT(d >= 0);
      new_size *= d;
    }
    AT_ASSERTM(
        new_size == numel_,
        "New size and old size are not equal. You cannot use Reshape, "
        "but should use Resize."
        // TODO(jiayq): remove the following warning after pending diffs
        // stabilize.
        " The old caffe2 mixes Reshape and Resize but this behavior has "
        "been changed. If you find this error, most likely you will need "
        "to change corresponding code from Reshape to Resize.");
    auto old_dim = sizes_.size();
    sizes_ = dims;
    update_to_contiguous_strides(old_dim);
  }

  /**
   * Release whatever memory the tensor was holding but keep size and type
   * information. Subsequent call to mutable_data will trigger new memory
   * allocation.
   */
  inline void FreeMemory() {
    // We'll detach from the old Storage and create a new one
    storage_ = at::Storage(storage_.device(), data_type_);
    storage_offset_ = 0;
  }

  /**
   * @brief Shares the data with another tensor.
   *
   * To share data between two tensors, the sizes of the two tensors must be
   * equal already. The reason we do not implicitly do a Resize to make the two
   * tensors have the same shape is that we want to allow tensors of different
   * shapes but the same number of items to still be able to share data. This
   * allows one to e.g. have a n-dimensional Tensor and a flattened version
   * sharing the same underlying storage.
   *
   * The source tensor should already have its data allocated.
   */
  void ShareData(const TensorImpl& src) {
    // Right now, we are assuming the device_type are the same, since it is
    // inherently the same in the non-templatized code. We should probably add
    // an assert here which might affect perf a little bit.
    AT_ASSERTM(
        src.numel_ == numel_,
        "Size mismatch - did you call reshape before sharing the data?");
    // It is possible that the source tensor hasn't called mutable_data() yet,
    // in which case ShareData() doesn't make much sense since we don't really
    // know what to share yet.
    AT_ASSERTM(
        src.storage_initialized(),
        "Source tensor has no content and has size > 0");
    // Finally, do sharing.
    /* Since we create new Storage whenever we need to change data_type/capacity
     * this still keeps the original semantics
     */
    storage_ = src.storage();
    data_type_ = src.dtype();
    storage_offset_ = src.storage_offset();
  }

  void ShareExternalPointer(
      at::DataPtr&& data_ptr,
      const caffe2::TypeMeta& data_type,
      size_t capacity) {
    AT_ASSERTM(
        data_type.id() != caffe2::TypeIdentifier::uninitialized(),
        "To share with a raw external pointer you need to pass in an "
        "initialized data_type(TypeMeta).");
    if (!capacity) {
      capacity = numel_ * data_type.itemsize();
    }
    if (storage_.unique()) {
      storage_.UniqueStorageShareExternalPointer(
          std::move(data_ptr), data_type, capacity);
      data_type_ = data_type;
      storage_offset_ = 0;
    } else {
      int64_t numel = capacity / data_type.itemsize();
      // Create a new Storage
      storage_ = at::Storage(data_type, numel, std::move(data_ptr), nullptr, true);
      data_type_ = data_type;
      storage_offset_ = 0;
    }
  }

  /**
   * Returns a mutable raw pointer of the underlying storage. Since we will need
   * to know the type of the data for allocation, a TypeMeta object is passed in
   * to specify the necessary information. This is conceptually equivalent of
   * calling mutable_data<T>() where the TypeMeta parameter meta is derived from
   * the type T. This function differs from mutable_data<T>() in the sense that
   * the type T can be specified during runtime via the TypeMeta object.
   *
   * If the existing data does not match the desired type, it will be deleted
   * and a new storage will be created.
   */
  inline void* raw_mutable_data(const caffe2::TypeMeta& meta) {
    // For 0-size tensors it's fine to return any pointer (including nullptr)
    if (data_type_ == meta && storage_initialized()) {
      return static_cast<void*>(static_cast<char*>(storage_.data()) + storage_offset_ * meta.itemsize());
    } else {
      bool had_special_dtor = data_type_.placementDelete() != nullptr;
      storage_offset_ = 0;
      if (storage_.unique()) {
        storage_.set_dtype(meta);
      } else {
        if (data_type_ != meta) {
          storage_ = at::Storage(storage_.device(), meta);
        }
      }
      data_type_ = meta;

      // We can reuse the existing buffer if the current data does not have
      // a special destructor and the new data doesn't have a special
      // constructor.
      if (numel_ == 0 ||
          (meta.placementNew() == nullptr && !had_special_dtor &&
           storage_.numel() >= numel_)) {
        AT_ASSERT(storage_offset_ == 0); // because we just reallocated
        return storage_.data();
      }
      const at::Allocator* allocator = storage_.allocator();
      // TODO: Get rid of StaticContext
      AT_ASSERTM(
          allocator == nullptr,
          "Allocator in storage_ is not used within Caffe2 functions. \
           we are using global function to get the allocator based on device \
           type.");
      allocator = caffe2::GetAllocator(storage_.device_type());
      if (meta.placementNew()) {
        // For types that need placement new, we will call it, as well as
        // making sure that when the data is freed, it calls the right
        // destruction procedure.
        auto size = numel_;
        auto dtor = data_type_.placementDelete();
        auto data_ptr = allocator->allocate(numel_ * storage_.itemsize());
        storage_.set_data_ptr(PlacementDeleteContext::makeDataPtr(
            std::move(data_ptr), dtor, size, storage_.device()));
        data_type_.placementNew()(storage_.data(), numel_);
      } else {
        // For fundamental type, new and delete is easier.
        storage_.set_data_ptr(
            allocator->allocate(numel_ * storage_.itemsize()));
      }
      storage_.set_numel(numel_);
      AT_ASSERT(storage_offset_ == 0); // because we just reallocated
      return storage_.data();
    }
  }

  /**
   * Returns a typed pointer of the underlying storage.
   *
   * For fundamental types, we reuse possible existing storage if there
   * is sufficient capacity.
   */
  template <typename T>
  inline T* mutable_data() {
    if (storage_initialized() && storage_.IsType<T>()) {
      return static_cast<T*>(storage_.data()) + storage_offset_;
    }
    // Check it here statically - otherwise TypeMeta would throw the runtime
    // error in attempt to invoke TypeMeta::ctor()
    static_assert(
        std::is_default_constructible<T>::value,
        "Tensor can't hold non-default-constructible types");
    return static_cast<T*>(raw_mutable_data(caffe2::TypeMeta::Make<T>()));
  }

  bool storage_initialized() const noexcept {
    return storage_.data() || numel_ == 0;
  }

  bool dtype_initialized() const noexcept {
    return data_type_ != caffe2::TypeMeta();
  }

 private:
  template <
      typename T,
      typename = typename std::enable_if<std::is_integral<T>::value>::type>
  bool SetDimsTemplate(at::ArrayRef<T> src) {
    auto old_numel = numel_;
    auto old_dim = sizes_.size();
    sizes_.resize(src.size());
    int64_t new_numel = 1;
    for (size_t i = 0; i < src.size(); ++i) {
      new_numel *= src[i];
      sizes_[i] = src[i];
    }
    update_to_contiguous_strides(old_dim);
    numel_ = new_numel;
    return numel_ != old_numel;
  }

  bool SetDims(at::ArrayRef<int64_t> s) {
    return SetDimsTemplate(s);
  }

  bool SetDims(at::ArrayRef<int> s) {
    return SetDimsTemplate(s);
  }

  bool SetDims(at::ArrayRef<size_t> s) {
    return SetDimsTemplate(s);
  }

  bool SetDims() {
    return SetDims(at::IntList{});
  }

  bool SetDims(const int64_t d0) {
    return SetDims(at::IntList{d0});
  }

  bool SetDims(const int64_t d0, const int64_t d1) {
    return SetDims(at::IntList{d0, d1});
  }

  bool SetDims(const int64_t d0, const int64_t d1, const int64_t d2) {
    return SetDims(at::IntList{d0, d1, d2});
  }

  bool SetDims(const int64_t d0, const int64_t d1, const int64_t d2, const int64_t d3) {
    return SetDims(at::IntList{d0, d1, d2, d3});
  }

  inline void update_to_contiguous_strides(size_t old_dim) {
    if (old_dim != sizes_.size()) {
      strides_ = c10::guts::make_unique<int64_t[]>(sizes_.size());
    }
    if (dim() > 0) {
      int last_idx = dim() - 1;
      strides_[last_idx] = 1;
      for (auto i = last_idx - 1; i >= 0; --i) {
        strides_[i] = strides_[i + 1] * std::max<int64_t>(sizes_[i + 1], 1);
      }
    }
    is_contiguous_ = true;
  }

public:
  at::Storage storage_; // TODO: Fix visibility on me

protected:
  std::vector<int64_t> sizes_;
  std::unique_ptr<int64_t[]> strides_; // this saves two words

  int64_t storage_offset_ = 0;
  // If sizes and strides are empty, the numel is 1!!  However, most of the
  // time, we will immediately set sizes to {0} and reset numel to 0.
  // (Can't do that in the default initializers, because there's no way to
  // spell "allocate a one-element array" for strides_).
  int64_t numel_ = 1;

  // INVARIANT: When storage is non-null, this type meta must
  // agree with the type meta in storage
  caffe2::TypeMeta data_type_;

  // You get to have eight byte-size fields here, before you
  // should pack this into a bitfield.
  TensorTypeId type_id_;
  bool is_contiguous_ = true;
  bool is_variable_ = false;
  bool is_wrapped_number_ = false;
  // we decide to keep reserved_ and it will
  // live in Tensor after the split
  // The logic is that if Extend() or ReserveSpace() were ever called,
  // then subsequent Resize()s will not free up Storage.
  bool reserved_ = false;

};
} // namespace at
