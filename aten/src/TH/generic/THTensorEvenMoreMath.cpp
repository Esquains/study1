#ifndef TH_GENERIC_FILE
#define TH_GENERIC_FILE "TH/generic/THTensorEvenMoreMath.cpp"
#else

#include <TH/generic/THTensorApply.hpp>
#include <ATen/NamedTensorUtils.h>
#include <ATen/WrapDimUtils.h>
#include <ATen/MemoryOverlap.h>

#if !defined(TH_REAL_IS_HALF) /* non half part */

#if !defined(TH_REAL_IS_BOOL)
void THTensor_(mul)(THTensor *r_, THTensor *t, scalar_t value)
{
  THTensor_(resizeAs)(r_, t);
  int64_t r_Size = THTensor_(nElement)(r_);
  int r_Contig = THTensor_(isContiguous)(r_);
  int tContig = THTensor_(isContiguous)(t);
  if (r_Contig && tContig) {
    TH_TENSOR_APPLY2_CONTIG(scalar_t, r_, scalar_t, t, THVector_(muls)(r__data, t_data, value, r__len););
  } else {
    TH_TENSOR_APPLY2_PARALLEL(r_Size, r_Contig, tContig, scalar_t, r_, scalar_t, t, *r__data = *t_data * value;, ORDIN_TH_OMP_OVERHEAD_THRESHOLD)
  }
}

#endif

#if !defined(TH_REAL_IS_BFLOAT16) /* non bfloat16 part*/

void THTensor_(indexCopy)(THTensor *tensor, int dim, THLongTensor *index, THTensor *src)
{
  ptrdiff_t i, numel;
  THTensor *tSlice, *sSlice;
  int64_t *index_data;

  dim = at::maybe_wrap_dim(dim, tensor);
  // Error checking for this function has moved to ATen!!
  numel = THLongTensor_nElement(index);

  index = THLongTensor_newContiguous(index);
  index_data = THLongTensor_data(index);

  if (tensor->dim() > 1 )
  {
    tSlice = THTensor_(new)();
    sSlice = THTensor_(new)();

    for (i=0; i<numel; i++)
    {
      THTensor_(select)(tSlice, tensor, dim, index_data[i]);
      THTensor_(select)(sSlice, src, dim, i);
      at::Tensor tSlice_wrap = THTensor_wrap(tSlice);
      at::Tensor sSlice_wrap = THTensor_wrap(sSlice);
      at::native::copy_(tSlice_wrap, sSlice_wrap);
    }

    c10::raw::intrusive_ptr::decref(tSlice);
    c10::raw::intrusive_ptr::decref(sSlice);
  }
  else
  {
    for (i=0; i<numel; i++)
    {
      THTensor_(set1d)(tensor, index_data[i], THTensor_(get1d)(src,i));
    }
  }
  THLongTensor_free(index);
}

static ptrdiff_t THTensor_(dataOffset)(THTensor* tensor, ptrdiff_t linearIndex) {
  auto size = THTensor_sizesLegacyNoScalars(tensor);
  auto stride = THTensor_stridesLegacyNoScalars(tensor);
  int nDim = THTensor_nDimensionLegacyAll(tensor);
  ptrdiff_t dataOffset = 0;
  for (int i = nDim - 1; i >= 0; i--) {
    dataOffset += (linearIndex % size[i]) * stride[i];
    linearIndex /= size[i];
  }
  return dataOffset;
}

static inline void THTensor_(checkLinearIndex)(int64_t linearIndex, int64_t numel) {
  THArgCheck(linearIndex < numel && linearIndex >= -numel, 2, "out of range: %d out of %d", (int)linearIndex, (int)numel);
}

static inline int64_t THTensor_(wrapLinearIndex)(int64_t linearIndex, int64_t numel) {
  return linearIndex < 0 ? linearIndex + numel : linearIndex;
}

void THTensor_(put)(THTensor *tensor, THLongTensor *index, THTensor *src, int accumulate)
{
  THArgCheck(THLongTensor_nElement(index) == THTensor_(nElement)(src), 3,
    "src should have the same number of elements as index");

  index = THLongTensor_newContiguous(index);
  src = THTensor_(newContiguous)(src);
  scalar_t* data = tensor->data<scalar_t>();
  ptrdiff_t numel = THTensor_(nElement)(tensor);
  int is_contiguous = THTensor_(isContiguous)(tensor);

  TH_TENSOR_APPLY2(int64_t, index, scalar_t, src,
    THTensor_(checkLinearIndex)(*index_data, numel);
    int64_t linearIndex = THTensor_(wrapLinearIndex)(*index_data, numel);
    int64_t dataOffset = is_contiguous ? linearIndex : THTensor_(dataOffset)(tensor, linearIndex);
    if (accumulate) {
      data[dataOffset] += *src_data;
    } else {
      data[dataOffset] = *src_data;
    }
  );

  c10::raw::intrusive_ptr::decref(src);
  THLongTensor_free(index);
}

void THTensor_(indexFill)(THTensor *tensor, int dim, THLongTensor *index, scalar_t val)
{
  at::NoNamesGuard guard;

  ptrdiff_t i, numel;
  THTensor *tSlice;
  int64_t *index_data;

  dim = at::maybe_wrap_dim(dim, tensor);
  numel = THLongTensor_nElement(index);
  THArgCheck(THTensor_nDimensionLegacyNoScalars(index) == 1, 3, "Index is supposed to be a vector");
  THArgCheck(dim < THTensor_nDimensionLegacyNoScalars(tensor), 4,"Indexing dim %d is out of bounds of tensor", dim);
  at::assert_no_overlap(tensor, index);
  if (at::has_internal_overlap(tensor) == at::MemOverlap::YES) {
    TORCH_WARN(
      "Use of index_fill_ on expanded tensors is deprecated. "
      "Please clone() the tensor before performing this operation. "
      "This also applies to advanced indexing e.g. tensor[mask] = scalar");
  }

  index = THLongTensor_newContiguous(index);
  index_data = THLongTensor_data(index);

  for (i=0; i<numel; i++)
  {
    if (tensor->dim() > 1)
    {
      tSlice = THTensor_(new)();
      THTensor_(select)(tSlice, tensor,dim,index_data[i]);
      THTensor_wrap(tSlice).fill_(val);
      c10::raw::intrusive_ptr::decref(tSlice);
    }
    else
    {
      THTensor_(set1d)(tensor, index_data[i], val);
    }
  }
  THLongTensor_free(index);
}

#endif

#endif

#endif /* TH_GENERIC_FILE */
