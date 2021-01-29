#include <ATen/ATen.h>
#include <ATen/SparseTensorImpl.h>
#include <ATen/SparseCsrTensorImpl.h>
#include <ATen/SparseTensorUtils.h>
#include <ATen/InitialTensorOptions.h>
#include <ATen/core/LegacyTypeDispatch.h>

namespace at {
namespace {
  DeviceType SparseCsrTensorSetToDeviceType(DispatchKeySet key_set) {
    if (key_set.has(DispatchKey::SparseCsrCPU)) {
      return kCPU;
    } else if (key_set.has(DispatchKey::SparseCsrCUDA)) {
      return kCUDA;
    } else {
      AT_ERROR("Cannot construct SparseTensor with non-sparse tensor type ID ", key_set);
    }
  }
}

SparseCsrTensorImpl::SparseCsrTensorImpl(at::DispatchKeySet key_set,
                                         const caffe2::TypeMeta& data_type)
  :   SparseCsrTensorImpl(key_set, data_type
      , at::empty({0}, at::initialTensorOptions().device(SparseCsrTensorSetToDeviceType(key_set)).dtype(ScalarType::Int)) // crow_indices
      , at::empty({0}, at::initialTensorOptions().device(SparseCsrTensorSetToDeviceType(key_set)).dtype(ScalarType::Int)) // col_indices
      , at::empty({0}, at::initialTensorOptions().device(SparseCsrTensorSetToDeviceType(key_set)).dtype(data_type)) // values
) {}

SparseCsrTensorImpl::SparseCsrTensorImpl(at::DispatchKeySet key_set,
                                         const caffe2::TypeMeta& data_type,
                                         at::Tensor crow_indices, at::Tensor col_indices, 
                                         at::Tensor values)
  : TensorImpl(key_set, data_type, values.device()),
    crow_indices_(std::move(crow_indices)),
    col_indices_(std::move(col_indices)),
    values_(std::move(values)) {}

void SparseCsrTensorImpl::resize_and_clear_(int64_t nnz_size, IntArrayRef size) {
  // call crow_indices().options() here since the struct contructor calls the tensor constructor
  // with args for device specific init.
  auto empty_crow_indices = at::empty(size[0] + 1, crow_indices().options());
  auto empty_col_indices = at::empty(nnz_size, col_indices().options());
  auto empty_values = at::empty(nnz_size, values().options());

  crow_indices_ = empty_crow_indices;
  col_indices_ = empty_col_indices;
  values_ = empty_values;
  sizes_and_strides_.set_sizes(size);
}

void SparseCsrTensorImpl::resize_as_(const Tensor& src) {
  crow_indices_ = at::empty_like(src.crow_indices(), src.crow_indices().options(), 
    src.crow_indices().suggest_memory_format());
  col_indices_ = at::empty_like(src.col_indices(), src.col_indices().options(), 
    src.col_indices().suggest_memory_format());
  values_ = at::empty_like(src.values(), src.values().options(), src.values().suggest_memory_format());
  sizes_and_strides_.set_sizes(src.sizes());
}
  
void SparseCsrTensorImpl::set_member_tensors_unsafe(const Tensor& crow_indices, const Tensor& col_indices,
                                                      const Tensor& values) {
  auto crow_indices_type = crow_indices.scalar_type();
  auto col_indices_type = col_indices.scalar_type();

  TORCH_CHECK(crow_indices_type == col_indices_type, "both crow_indices and col_indices should have the same type.");
  TORCH_CHECK(crow_indices_type == kInt || crow_indices_type == kLong, 
              "crow_indices must be an int32 or int64 type, but got: ", crow_indices_type);
  TORCH_CHECK(col_indices_type == kInt || col_indices_type == kLong,
              "col_indices must be int32 or int64 type, but got: ", col_indices_type);
  TORCH_CHECK(values.scalar_type() == typeMetaToScalarType(dtype()), 
              "dtype of values (", values.scalar_type(), ") must match dtype of sparse tensor (", 
              typeMetaToScalarType(dtype()), ")");

  TORCH_CHECK(col_indices.layout() == kStrided, 
              "expected col_indices to be a dense tensor, but got indices of layout ", 
              col_indices.layout());
  TORCH_CHECK(crow_indices.layout() == kStrided, 
              "expected crow_indices to be a dense tensor, but got crow_indices of layout ", 
              crow_indices.layout());
  TORCH_CHECK(values.layout() == kStrided, 
              "expected values to be a dense tensor, but got values of layout ", 
              values.layout());

  TORCH_CHECK(values.device().type() == device().type(), "device type of values (", values.device().type(),
              ") must match device type of device().type()", device().type(), ")");
  TORCH_CHECK(!col_indices.is_cuda() || col_indices.get_device() == values.get_device(), "device of col_indices (", 
              col_indices.get_device(), ") must match device of values (", values.get_device(), ")");
  TORCH_CHECK(!crow_indices.is_cuda() || crow_indices.get_device() == values.get_device(), "device of crow_indices (", 
              crow_indices.get_device(), ") must match device of values (", values.get_device(), ")");

  TORCH_CHECK(col_indices.size(0) == values.size(0), 
              "col_indices and values must have same nnz, but got nnz from indices: ",
              col_indices.size(0), ", nnz from values: ", values.size(0));

  TORCH_CHECK(crow_indices.dim() == 1, "crow_indices must have dim=1 but got crow_indices.dim()=", 
              crow_indices.dim());
  TORCH_CHECK(col_indices.dim() == 1, "col_indices must have dim=1 but got col_indices.dim()=",
              col_indices.dim()); 
  TORCH_CHECK(values.dim() == 1, "values must have dim=1 but got values.dim()=", values.dim());

  crow_indices_ = crow_indices;
  col_indices_ = col_indices;
  values_ = values;
}
}
