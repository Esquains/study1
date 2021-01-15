#include <ATen/SparseTensorUtils.h>

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

#include <functional>
#include <limits>
#include <algorithm>
#include <vector>
#include <numeric>
#include <cmath>
#include <iostream>

#if !AT_MKL_ENABLED()

namespace at { namespace native {
    using namespace at::sparse;
    Tensor _sparse_mm_mkl_(Tensor& self, const SparseTensor& sparse_, const Tensor& dense, const Tensor& t,
                            Scalar alpha, Scalar beta) {
      AT_ERROR("sparse_mm_mkl: ATen not compiled with MKL support");
    }
}}

#else  // AT_MKL_ENABLED

#include <mkl.h>
#include <mkl_spblas.h>
#include <ATen/mkl/Exceptions.h>
#include <ATen/mkl/Descriptors.h>
#include <ATen/mkl/Limits.h>

namespace at { namespace native {
    using namespace at::sparse;
    
    static inline void sparse_mm_mkl_impl(float * res, int32_t * col_indices, int32_t * crow_indices, float * values,
                                          float * dense, float * t, float alpha, float beta,
                                          int32_t nrows, int32_t ncols, int32_t dense_ncols) {
      sparse_matrix_t A = 0;
      matrix_descr desc;
      desc.type = SPARSE_MATRIX_TYPE_GENERAL;
      int retval = mkl_sparse_s_create_csr(&A, SPARSE_INDEX_BASE_ZERO, nrows, ncols, crow_indices,
                              crow_indices+1, col_indices, values);
      TORCH_CHECK(retval == 0, "mkl_sparse_d_create_csr failed with error code: ", retval);

      mkl_sparse_s_mm(SPARSE_OPERATION_NON_TRANSPOSE, alpha, A, desc,
                      SPARSE_LAYOUT_ROW_MAJOR, dense, dense_ncols, dense_ncols, beta,
                      res, dense_ncols);
      mkl_sparse_destroy(A);
    }

    // TODO: The types here are long int32_t but int32_t = LongTensor only when using lp64 for MKL. SHould we resort
    // to using IntTensor for the indices and crow_indices?
    static inline void sparse_mm_mkl_impl(double * res, int32_t * col_indices, int32_t * crow_indices,
                                          double * values, 
                                          double * dense, double * t, double alpha, double beta, 
                                          int32_t nrows, int32_t ncols, int32_t dense_ncols) {
      sparse_matrix_t A = 0;
      matrix_descr desc;
      desc.type = SPARSE_MATRIX_TYPE_GENERAL;
      int  retval = mkl_sparse_d_create_csr(&A, SPARSE_INDEX_BASE_ZERO, nrows, ncols, crow_indices, 
                              crow_indices+1, col_indices, values);
      TORCH_CHECK(retval == 0, "mkl_sparse_d_create_csr failed with error code: ", retval);

      mkl_sparse_d_mm(SPARSE_OPERATION_NON_TRANSPOSE, alpha, A, desc,
                      SPARSE_LAYOUT_ROW_MAJOR, dense, dense_ncols, dense_ncols,
                      beta, res, dense_ncols);
      mkl_sparse_destroy(A);
    }

    template <typename scalar_t>
    static inline void sparse_mm_mkl_template(Tensor& res, const Tensor& col_indices,
                                              const Tensor& crow_indices, const Tensor& values,
                                              const Tensor& dense, const Tensor& t, Scalar alpha,
                                              Scalar beta, IntArrayRef size, IntArrayRef dense_size) {
      sparse_mm_mkl_impl(res.data_ptr<scalar_t>(),
                         col_indices.data_ptr<int32_t>(),
                         crow_indices.data_ptr<int32_t>(),
                         values.data_ptr<scalar_t>(),
                         dense.data_ptr<scalar_t>(),
                         t.data_ptr<scalar_t>(),
                         alpha.to<scalar_t>(),
                         beta.to<scalar_t>(),
                         size[0], size[1], dense_size[1]);
    }

  Tensor _sparse_mm_mkl_(Tensor& self, const SparseTensor& sparse_, const Tensor& dense,
                        const Tensor& t, Scalar alpha, Scalar beta) {
    AT_DISPATCH_FLOATING_TYPES(
      dense.scalar_type(), "addmm_sparse_gcs_dense", [&] {
        sparse_mm_mkl_template<scalar_t>(self, sparse_.col_indices(),
                                         sparse_.crow_indices(), sparse_.values(), dense, t,
                                         alpha, beta, sparse_.sizes(), dense.sizes());
    });
    return self;
  }
}}
#endif
