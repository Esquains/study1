#include <ATen/native/sparse/SparseGCSTensorMath.h>

#include <ATen/SparseTensorImpl.h>
#include <ATen/ATen.h>
#include <ATen/ExpandUtils.h>
#include <ATen/Dispatch.h>
#include <ATen/NativeFunctions.h>
#include <ATen/native/CPUBlas.h>
#include <ATen/native/LinearAlgebraUtils.h>
#include <ATen/native/Resize.h>
#include <ATen/TensorUtils.h>
#include <ATen/Parallel.h>
#include <ATen/LegacyTHFunctionsCPU.h>
#include <ATen/core/grad_mode.h>
#include <ATen/NamedTensorUtils.h>
#include <TH/THBlasUtils.h>

#include <functional>
#include <numeric>
#include <vector>
#include <limits>

namespace at { namespace native {
  using namespace at::sparse;

  // res - result (output) tensor, sparse_ - sparse tensor to be multiplied.
  // temp_dense - temp dense (input) matrix with expanded dimensions.
  Tensor _sparse_gcs_mm_cpu(Tensor& res, const SparseTensor& sparse_, const Tensor& temp_res,
                            const Tensor& dense, Scalar alpha, Scalar beta) {

    TORCH_CHECK(sparse_.dim() == 2, "sparse_gcs_mm_cpu: sparse matrix dimensionality must be 2, got ",
                sparse_.dim());
    TORCH_CHECK(sparse_.size(1) == dense.size(0),
                "sparse_gcs_mm_cpu: ncols of sparse matrix must be equal to nrows of dense matrix,\
                but got sparse dims: ", sparse_.sizes(), " and dense dims: ", dense.sizes());
 
    auto indices = sparse_.indices();
    auto pointers = sparse_.pointers();
    auto values   = sparse_.values();
    int64_t nnz = sparse_._nnz();
    int64_t dim_k = dense.size(0);
    
    AT_DISPATCH_FLOATING_TYPES(
      values.scalar_type(), "addmm_sparse_gcs_dense", [&] {
        scalar_t cast_alpha = alpha.to<scalar_t>();
        scalar_t cast_beta = beta.to<scalar_t>();

        if (cast_beta == 0) {
          res.zero_();
        } else if (cast_beta == 1) {
          if (!is_same_tensor(res, temp_res)) {
            res.copy_(temp_res);
          }
        } else {
          at::mul_out(res, temp_res, scalar_to_tensor(beta));
        }
    });

    if (at::hasMKL()) {
      at::_sparse_mm_mkl_(res, sparse_, dense, temp_res, alpha, beta);
    }
    else {
      int64_t dense_stride0 = dense.stride(0);
      int64_t dense_stride1 = dense.stride(1);
      int64_t res_stride0 = res.stride(0);
      int64_t res_stride1 = res.stride(1);
      
      AT_DISPATCH_FLOATING_TYPES(
        values.scalar_type(), "sparse_gcs_mm_cpu", [&] {
          scalar_t cast_alpha = alpha.to<scalar_t>();
          scalar_t cast_beta = beta.to<scalar_t>();
          scalar_t* dense_ptr = dense.data_ptr<scalar_t>();
          scalar_t* res_ptr = res.data_ptr<scalar_t>();

          auto indices_accessor = indices.accessor<int32_t, 1>();
          auto pointers_accessor = pointers.accessor<int32_t, 1>();
          auto values_accessor = values.accessor<scalar_t, 1>();

          for (int iptr = 0; iptr < pointers.size(0)-1; ++iptr) {
            int start_index = pointers_accessor[iptr];
            int end_index = pointers_accessor[iptr+1];
            int nindices = end_index - start_index;

            for (int i = start_index; i < end_index; ++i) {
              auto val = values_accessor[i];
              auto icol = indices_accessor[i];

              THBlas_axpy<scalar_t>(dim_k,
                cast_alpha * val, dense_ptr + icol * dense_stride0, dense_stride1,
                res_ptr + iptr * res_stride0, res_stride1);
            }
          }
      });
    }

    return res;
  }

}}                              // namespace at::native 
