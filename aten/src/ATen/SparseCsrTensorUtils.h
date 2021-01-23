#pragma once

#include <ATen/ATen.h>
#include <ATen/SparseTensorImpl.h>
#include <ATen/SparseCsrTensorImpl.h>
#include <ATen/SparseTensorUtils.h>

namespace at { namespace sparse {
  inline SparseCsrTensorImpl* get_sparse_csr_impl(const SparseTensor& self) {
    AT_ASSERTM(self.is_sparse_csr(), 
      "_internal_get_SparseCsrTensorImpl: not a sparse CSR tensor");
    return static_cast<SparseCsrTensorImpl*>(self.unsafeGetTensorImpl());
  }
} } // namespace at::sparse