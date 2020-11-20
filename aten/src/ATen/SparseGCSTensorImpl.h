#pragma once

#include <ATen/Tensor.h>
#include <ATen/TensorUtils.h>
#include <ATen/SparseTensorUtils.h>
#include <ATen/SparseTensorImpl.h>
#include <c10/core/TensorImpl.h>
#include <c10/util/Exception.h>

namespace at {
// TODO: since many methods in SparseTensorImpl can be used by GCS sparse tensor directly
// we probably should have some superclass between TensorImpl and GCSTensorImpl that is
// shared between COO and GCS tensors.
struct CAFFE2_API SparseGCSTensorImpl : public TensorImpl {
  Tensor pointers_;
  Tensor indices_;
  Tensor values_;
  Tensor reduction_;

  // Data for making index conversion operations faster.

  // strides of the first half of the split dimensions.
  std::vector<int> strides0_;
  // strides of the second half of the split dimensions.
  std::vector<int> strides1_;
  // dims of the first half of the split dimensions.
  std::vector<int> dims0_;
  // dims of the second half of the split dimensions.
  std::vector<int> dims1_;
  // Dimension at which we split the tensor dimensions into two groups for reduction to
  // a 2D GCS tensor.
  int rsplit_dim_;           
 public:
  explicit SparseGCSTensorImpl(at::DispatchKeySet, const caffe2::TypeMeta&);

  void resize_(IntArrayRef size);
  void resize_and_clear_(int64_t nnz_size, int64_t ptr_size, int64_t redux_size, IntArrayRef size);
  void resize_as_(const Tensor& src);
  

  void set_member_tensors_unsafe(const Tensor& pointers, const Tensor& indices,
                                 const Tensor& values, const Tensor& reduction);

  std::vector<int> strides0() const { return strides0_; }
  std::vector<int> strides1() const { return strides1_; }
  std::vector<int> dims0() const { return dims0_; }
  std::vector<int> dims1() const { return dims1_; }
  int rsplit_dim() const { return rsplit_dim_; }
  
  Tensor pointers() const { return pointers_; }
  Tensor indices() const { return indices_; }
  Tensor values() const { return values_; }
  Tensor reduction() const { return reduction_; }
  int nnz() const { return values_.size(0); } // TODO: methods like these also exist in COO tensor. Deduplicate?

 private :
  
  explicit SparseGCSTensorImpl(at::DispatchKeySet key_set, const caffe2::TypeMeta& data_type,
                               at::Tensor pointers, at::Tensor indices, at::Tensor values, at::Tensor reduction);

  void make_strides(int shape_begin, std::vector<int>& strides, std::vector<int>& dims);
};
}
