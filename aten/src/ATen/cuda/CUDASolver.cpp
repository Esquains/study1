#include <ATen/Context.h>
#include <ATen/NativeFunctions.h>
#include <ATen/cuda/CUDASolver.h>
#include <c10/cuda/CUDACachingAllocator.h>

#ifdef CUDART_VERSION

namespace at {
namespace cuda {
namespace solver {

template <>
void getrf<double>(
    cusolverDnHandle_t handle, int m, int n, double* dA, int ldda, int* ipiv, int* info) {
  int lwork;
  TORCH_CUSOLVER_CHECK(
      cusolverDnDgetrf_bufferSize(handle, m, n, dA, ldda, &lwork));
  auto& allocator = *::c10::cuda::CUDACachingAllocator::get();
  auto dataPtr = allocator.allocate(sizeof(double)*lwork);
  TORCH_CUSOLVER_CHECK(cusolverDnDgetrf(
      handle, m, n, dA, ldda, static_cast<double*>(dataPtr.get()), ipiv, info));
}

template <>
void getrf<float>(
    cusolverDnHandle_t handle, int m, int n, float* dA, int ldda, int* ipiv, int* info) {
  int lwork;
  TORCH_CUSOLVER_CHECK(
      cusolverDnSgetrf_bufferSize(handle, m, n, dA, ldda, &lwork));
  auto& allocator = *::c10::cuda::CUDACachingAllocator::get();
  auto dataPtr = allocator.allocate(sizeof(float)*lwork);
  TORCH_CUSOLVER_CHECK(cusolverDnSgetrf(
      handle, m, n, dA, ldda, static_cast<float*>(dataPtr.get()), ipiv, info));
}

template <>
void getrf<c10::complex<double>>(
    cusolverDnHandle_t handle,
    int m,
    int n,
    c10::complex<double>* dA,
    int ldda,
    int* ipiv,
    int* info) {
  int lwork;
  TORCH_CUSOLVER_CHECK(cusolverDnZgetrf_bufferSize(
      handle, m, n, reinterpret_cast<cuDoubleComplex*>(dA), ldda, &lwork));
  auto& allocator = *::c10::cuda::CUDACachingAllocator::get();
  auto dataPtr = allocator.allocate(sizeof(cuDoubleComplex) * lwork);
  TORCH_CUSOLVER_CHECK(cusolverDnZgetrf(
      handle,
      m,
      n,
      reinterpret_cast<cuDoubleComplex*>(dA),
      ldda,
      static_cast<cuDoubleComplex*>(dataPtr.get()),
      ipiv,
      info));
}

template <>
void getrf<c10::complex<float>>(
    cusolverDnHandle_t handle,
    int m,
    int n,
    c10::complex<float>* dA,
    int ldda,
    int* ipiv,
    int* info) {
  int lwork;
  TORCH_CUSOLVER_CHECK(cusolverDnCgetrf_bufferSize(
      handle, m, n, reinterpret_cast<cuComplex*>(dA), ldda, &lwork));
  auto& allocator = *::c10::cuda::CUDACachingAllocator::get();
  auto dataPtr = allocator.allocate(sizeof(cuComplex) * lwork);
  TORCH_CUSOLVER_CHECK(cusolverDnCgetrf(
      handle,
      m,
      n,
      reinterpret_cast<cuComplex*>(dA),
      ldda,
      static_cast<cuComplex*>(dataPtr.get()),
      ipiv,
      info));
}

template <>
void getrs<double>(
    cusolverDnHandle_t handle, int n, int nrhs, double* dA, int lda, int* ipiv, double* ret, int ldb, int* info) {
  TORCH_CUSOLVER_CHECK(cusolverDnDgetrs(
    handle, CUBLAS_OP_N, n, nrhs, dA, lda, ipiv, ret, ldb, info));
}

template <>
void getrs<float>(
    cusolverDnHandle_t handle, int n, int nrhs, float* dA, int lda, int* ipiv, float* ret, int ldb, int* info) {
  TORCH_CUSOLVER_CHECK(cusolverDnSgetrs(
    handle, CUBLAS_OP_N, n, nrhs, dA, lda, ipiv, ret, ldb, info));
}

template <>
void getrs<c10::complex<double>>(
    cusolverDnHandle_t handle,
    int n,
    int nrhs,
    c10::complex<double>* dA,
    int lda,
    int* ipiv,
    c10::complex<double>* ret,
    int ldb,
    int* info) {
  TORCH_CUSOLVER_CHECK(cusolverDnZgetrs(
      handle,
      CUBLAS_OP_N,
      n,
      nrhs,
      reinterpret_cast<cuDoubleComplex*>(dA),
      lda,
      ipiv,
      reinterpret_cast<cuDoubleComplex*>(ret),
      ldb,
      info));
}

template <>
void getrs<c10::complex<float>>(
    cusolverDnHandle_t handle,
    int n,
    int nrhs,
    c10::complex<float>* dA,
    int lda,
    int* ipiv,
    c10::complex<float>* ret,
    int ldb,
    int* info) {
  TORCH_CUSOLVER_CHECK(cusolverDnCgetrs(
      handle,
      CUBLAS_OP_N,
      n,
      nrhs,
      reinterpret_cast<cuComplex*>(dA),
      lda,
      ipiv,
      reinterpret_cast<cuComplex*>(ret),
      ldb,
      info));
}


template<>
void gesvdj<float>(
    cusolverDnHandle_t handle, cusolverEigMode_t jobz, int econ, int m, int n, float* A, int lda, float* S, float* U,
    int ldu, float *V, int ldv, int *info, gesvdjInfo_t params
) {
  int lwork;
  TORCH_CUSOLVER_CHECK(cusolverDnSgesvdj_bufferSize(handle, jobz, econ, m, n, A, lda, S, U, ldu, V, ldv, &lwork, params));

  auto& allocator = *::c10::cuda::CUDACachingAllocator::get();
  auto dataPtr = allocator.allocate(sizeof(float)*lwork);

  TORCH_CUSOLVER_CHECK(cusolverDnSgesvdj(
    handle, jobz, econ, m, n, A, lda, S, U, ldu, V, ldv,
    static_cast<float*>(dataPtr.get()),
    lwork, info, params));
}

template<>
void gesvdj<double>(
    cusolverDnHandle_t handle, cusolverEigMode_t jobz, int econ, int m, int n, double* A, int lda, double* S, double* U,
    int ldu, double *V, int ldv, int *info, gesvdjInfo_t params
) {
  int lwork;
  TORCH_CUSOLVER_CHECK(cusolverDnDgesvdj_bufferSize(handle, jobz, econ, m, n, A, lda, S, U, ldu, V, ldv, &lwork, params));

  auto& allocator = *::c10::cuda::CUDACachingAllocator::get();
  auto dataPtr = allocator.allocate(sizeof(double)*lwork);

  TORCH_CUSOLVER_CHECK(cusolverDnDgesvdj(
    handle, jobz, econ, m, n, A, lda, S, U, ldu, V, ldv,
    static_cast<double*>(dataPtr.get()),
    lwork, info, params));
}

template<>
void gesvdj<c10::complex<float>>(
    cusolverDnHandle_t handle, cusolverEigMode_t jobz, int econ, int m, int n, c10::complex<float>* A, int lda, float* S, c10::complex<float>* U,
    int ldu, c10::complex<float> *V, int ldv, int *info, gesvdjInfo_t params
) {
  int lwork;
  TORCH_CUSOLVER_CHECK(cusolverDnCgesvdj_bufferSize(
    handle, jobz, econ, m, n,
    reinterpret_cast<cuComplex*>(A),
    lda, S,
    reinterpret_cast<cuComplex*>(U),
    ldu,
    reinterpret_cast<cuComplex*>(V),
    ldv, &lwork, params));

  auto& allocator = *::c10::cuda::CUDACachingAllocator::get();
  auto dataPtr = allocator.allocate(sizeof(cuComplex)*lwork);

  TORCH_CUSOLVER_CHECK(cusolverDnCgesvdj(
    handle, jobz, econ, m, n,
    reinterpret_cast<cuComplex*>(A),
    lda, S,
    reinterpret_cast<cuComplex*>(U),
    ldu,
    reinterpret_cast<cuComplex*>(V),
    ldv,
    static_cast<cuComplex*>(dataPtr.get()),
    lwork, info, params));
}

template<>
void gesvdj<c10::complex<double>>(
    cusolverDnHandle_t handle, cusolverEigMode_t jobz, int econ, int m, int n, c10::complex<double>* A, int lda, double* S, c10::complex<double>* U,
    int ldu, c10::complex<double> *V, int ldv, int *info, gesvdjInfo_t params
) {
  int lwork;
  TORCH_CUSOLVER_CHECK(cusolverDnZgesvdj_bufferSize(
    handle, jobz, econ, m, n,
    reinterpret_cast<cuDoubleComplex*>(A),
    lda, S,
    reinterpret_cast<cuDoubleComplex*>(U),
    ldu,
    reinterpret_cast<cuDoubleComplex*>(V),
    ldv, &lwork, params));

  auto& allocator = *::c10::cuda::CUDACachingAllocator::get();
  auto dataPtr = allocator.allocate(sizeof(cuDoubleComplex)*lwork);

  TORCH_CUSOLVER_CHECK(cusolverDnZgesvdj(
    handle, jobz, econ, m, n,
    reinterpret_cast<cuDoubleComplex*>(A),
    lda, S,
    reinterpret_cast<cuDoubleComplex*>(U),
    ldu,
    reinterpret_cast<cuDoubleComplex*>(V),
    ldv,
    static_cast<cuDoubleComplex*>(dataPtr.get()),
    lwork, info, params));
}


template<>
void gesvdjBatched<float>(
    cusolverDnHandle_t handle, cusolverEigMode_t jobz, int m, int n, float* A, int lda, float* S, float* U,
    int ldu, float *V, int ldv, int *info, gesvdjInfo_t params, int batchSize
) {
  int lwork;
  TORCH_CUSOLVER_CHECK(cusolverDnSgesvdjBatched_bufferSize(handle, jobz, m, n, A, lda, S, U, ldu, V, ldv, &lwork, params, batchSize));

  auto& allocator = *::c10::cuda::CUDACachingAllocator::get();
  auto dataPtr = allocator.allocate(sizeof(float)*lwork);

  TORCH_CUSOLVER_CHECK(cusolverDnSgesvdjBatched(
    handle, jobz, m, n, A, lda, S, U, ldu, V, ldv,
    static_cast<float*>(dataPtr.get()),
    lwork, info, params, batchSize));
}

template<>
void gesvdjBatched<double>(
    cusolverDnHandle_t handle, cusolverEigMode_t jobz, int m, int n, double* A, int lda, double* S, double* U,
    int ldu, double *V, int ldv, int *info, gesvdjInfo_t params, int batchSize
) {
  int lwork;
  TORCH_CUSOLVER_CHECK(cusolverDnDgesvdjBatched_bufferSize(handle, jobz, m, n, A, lda, S, U, ldu, V, ldv, &lwork, params, batchSize));

  auto& allocator = *::c10::cuda::CUDACachingAllocator::get();
  auto dataPtr = allocator.allocate(sizeof(double)*lwork);

  TORCH_CUSOLVER_CHECK(cusolverDnDgesvdjBatched(
    handle, jobz, m, n, A, lda, S, U, ldu, V, ldv,
    static_cast<double*>(dataPtr.get()),
    lwork, info, params, batchSize));
}

template<>
void gesvdjBatched<c10::complex<float>>(
    cusolverDnHandle_t handle, cusolverEigMode_t jobz, int m, int n, c10::complex<float>* A, int lda, float* S, c10::complex<float>* U,
    int ldu, c10::complex<float> *V, int ldv, int *info, gesvdjInfo_t params, int batchSize
) {
  int lwork;
  TORCH_CUSOLVER_CHECK(cusolverDnCgesvdjBatched_bufferSize(
    handle, jobz, m, n,
    reinterpret_cast<cuComplex*>(A),
    lda, S,
    reinterpret_cast<cuComplex*>(U),
    ldu,
    reinterpret_cast<cuComplex*>(V),
    ldv, &lwork, params, batchSize));

  auto& allocator = *::c10::cuda::CUDACachingAllocator::get();
  auto dataPtr = allocator.allocate(sizeof(cuComplex)*lwork);

  TORCH_CUSOLVER_CHECK(cusolverDnCgesvdjBatched(
    handle, jobz, m, n,
    reinterpret_cast<cuComplex*>(A),
    lda, S,
    reinterpret_cast<cuComplex*>(U),
    ldu,
    reinterpret_cast<cuComplex*>(V),
    ldv,
    static_cast<cuComplex*>(dataPtr.get()),
    lwork, info, params, batchSize));
}

template<>
void gesvdjBatched<c10::complex<double>>(
    cusolverDnHandle_t handle, cusolverEigMode_t jobz, int m, int n, c10::complex<double>* A, int lda, double* S, c10::complex<double>* U,
    int ldu, c10::complex<double> *V, int ldv, int *info, gesvdjInfo_t params, int batchSize
) {
  int lwork;
  TORCH_CUSOLVER_CHECK(cusolverDnZgesvdjBatched_bufferSize(
    handle, jobz, m, n,
    reinterpret_cast<cuDoubleComplex*>(A),
    lda, S,
    reinterpret_cast<cuDoubleComplex*>(U),
    ldu,
    reinterpret_cast<cuDoubleComplex*>(V),
    ldv, &lwork, params, batchSize));

  auto& allocator = *::c10::cuda::CUDACachingAllocator::get();
  auto dataPtr = allocator.allocate(sizeof(cuDoubleComplex)*lwork);

  TORCH_CUSOLVER_CHECK(cusolverDnZgesvdjBatched(
    handle, jobz, m, n,
    reinterpret_cast<cuDoubleComplex*>(A),
    lda, S,
    reinterpret_cast<cuDoubleComplex*>(U),
    ldu,
    reinterpret_cast<cuDoubleComplex*>(V),
    ldv,
    static_cast<cuDoubleComplex*>(dataPtr.get()),
    lwork, info, params, batchSize));
}

template <>
void syevd_bufferSize<float>(
    cusolverDnHandle_t handle,
    cusolverEigMode_t jobz,
    cublasFillMode_t uplo,
    int n,
    const float* A,
    int lda,
    const float* W,
    int* lwork) {
  TORCH_CUSOLVER_CHECK(
      cusolverDnSsyevd_bufferSize(handle, jobz, uplo, n, A, lda, W, lwork));
}

template <>
void syevd_bufferSize<double>(
    cusolverDnHandle_t handle,
    cusolverEigMode_t jobz,
    cublasFillMode_t uplo,
    int n,
    const double* A,
    int lda,
    const double* W,
    int* lwork) {
  TORCH_CUSOLVER_CHECK(
      cusolverDnDsyevd_bufferSize(handle, jobz, uplo, n, A, lda, W, lwork));
}

template <>
void syevd_bufferSize<c10::complex<float>, float>(
    cusolverDnHandle_t handle,
    cusolverEigMode_t jobz,
    cublasFillMode_t uplo,
    int n,
    const c10::complex<float>* A,
    int lda,
    const float* W,
    int* lwork) {
  TORCH_CUSOLVER_CHECK(cusolverDnCheevd_bufferSize(
      handle,
      jobz,
      uplo,
      n,
      reinterpret_cast<const cuComplex*>(A),
      lda,
      W,
      lwork));
}

template <>
void syevd_bufferSize<c10::complex<double>, double>(
    cusolverDnHandle_t handle,
    cusolverEigMode_t jobz,
    cublasFillMode_t uplo,
    int n,
    const c10::complex<double>* A,
    int lda,
    const double* W,
    int* lwork) {
  TORCH_CUSOLVER_CHECK(cusolverDnZheevd_bufferSize(
      handle,
      jobz,
      uplo,
      n,
      reinterpret_cast<const cuDoubleComplex*>(A),
      lda,
      W,
      lwork));
}

template <>
void syevd<float>(
    cusolverDnHandle_t handle,
    cusolverEigMode_t jobz,
    cublasFillMode_t uplo,
    int n,
    float* A,
    int lda,
    float* W,
    float* work,
    int lwork,
    int* info) {
  TORCH_CUSOLVER_CHECK(
      cusolverDnSsyevd(handle, jobz, uplo, n, A, lda, W, work, lwork, info));
}

template <>
void syevd<double>(
    cusolverDnHandle_t handle,
    cusolverEigMode_t jobz,
    cublasFillMode_t uplo,
    int n,
    double* A,
    int lda,
    double* W,
    double* work,
    int lwork,
    int* info) {
  TORCH_CUSOLVER_CHECK(
      cusolverDnDsyevd(handle, jobz, uplo, n, A, lda, W, work, lwork, info));
}

template <>
void syevd<c10::complex<float>, float>(
    cusolverDnHandle_t handle,
    cusolverEigMode_t jobz,
    cublasFillMode_t uplo,
    int n,
    c10::complex<float>* A,
    int lda,
    float* W,
    c10::complex<float>* work,
    int lwork,
    int* info) {
  TORCH_CUSOLVER_CHECK(cusolverDnCheevd(
      handle,
      jobz,
      uplo,
      n,
      reinterpret_cast<cuComplex*>(A),
      lda,
      W,
      reinterpret_cast<cuComplex*>(work),
      lwork,
      info));
}

template <>
void syevd<c10::complex<double>, double>(
    cusolverDnHandle_t handle,
    cusolverEigMode_t jobz,
    cublasFillMode_t uplo,
    int n,
    c10::complex<double>* A,
    int lda,
    double* W,
    c10::complex<double>* work,
    int lwork,
    int* info) {
  TORCH_CUSOLVER_CHECK(cusolverDnZheevd(
      handle,
      jobz,
      uplo,
      n,
      reinterpret_cast<cuDoubleComplex*>(A),
      lda,
      W,
      reinterpret_cast<cuDoubleComplex*>(work),
      lwork,
      info));
}

template <>
void syevj_bufferSize<float>(
    cusolverDnHandle_t handle,
    cusolverEigMode_t jobz,
    cublasFillMode_t uplo,
    int n,
    const float* A,
    int lda,
    const float* W,
    int* lwork,
    syevjInfo_t params) {
  TORCH_CUSOLVER_CHECK(cusolverDnSsyevj_bufferSize(
      handle, jobz, uplo, n, A, lda, W, lwork, params));
}

template <>
void syevj_bufferSize<double>(
    cusolverDnHandle_t handle,
    cusolverEigMode_t jobz,
    cublasFillMode_t uplo,
    int n,
    const double* A,
    int lda,
    const double* W,
    int* lwork,
    syevjInfo_t params) {
  TORCH_CUSOLVER_CHECK(cusolverDnDsyevj_bufferSize(
      handle, jobz, uplo, n, A, lda, W, lwork, params));
}

template <>
void syevj_bufferSize<c10::complex<float>, float>(
    cusolverDnHandle_t handle,
    cusolverEigMode_t jobz,
    cublasFillMode_t uplo,
    int n,
    const c10::complex<float>* A,
    int lda,
    const float* W,
    int* lwork,
    syevjInfo_t params) {
  TORCH_CUSOLVER_CHECK(cusolverDnCheevj_bufferSize(
      handle,
      jobz,
      uplo,
      n,
      reinterpret_cast<const cuComplex*>(A),
      lda,
      W,
      lwork,
      params));
}

template <>
void syevj_bufferSize<c10::complex<double>, double>(
    cusolverDnHandle_t handle,
    cusolverEigMode_t jobz,
    cublasFillMode_t uplo,
    int n,
    const c10::complex<double>* A,
    int lda,
    const double* W,
    int* lwork,
    syevjInfo_t params) {
  TORCH_CUSOLVER_CHECK(cusolverDnZheevj_bufferSize(
      handle,
      jobz,
      uplo,
      n,
      reinterpret_cast<const cuDoubleComplex*>(A),
      lda,
      W,
      lwork,
      params));
}

template <>
void syevj<float>(
    cusolverDnHandle_t handle,
    cusolverEigMode_t jobz,
    cublasFillMode_t uplo,
    int n,
    float* A,
    int lda,
    float* W,
    float* work,
    int lwork,
    int* info,
    syevjInfo_t params) {
  TORCH_CUSOLVER_CHECK(cusolverDnSsyevj(
      handle, jobz, uplo, n, A, lda, W, work, lwork, info, params));
}

template <>
void syevj<double>(
    cusolverDnHandle_t handle,
    cusolverEigMode_t jobz,
    cublasFillMode_t uplo,
    int n,
    double* A,
    int lda,
    double* W,
    double* work,
    int lwork,
    int* info,
    syevjInfo_t params) {
  TORCH_CUSOLVER_CHECK(cusolverDnDsyevj(
      handle, jobz, uplo, n, A, lda, W, work, lwork, info, params));
}

template <>
void syevj<c10::complex<float>, float>(
    cusolverDnHandle_t handle,
    cusolverEigMode_t jobz,
    cublasFillMode_t uplo,
    int n,
    c10::complex<float>* A,
    int lda,
    float* W,
    c10::complex<float>* work,
    int lwork,
    int* info,
    syevjInfo_t params) {
  TORCH_CUSOLVER_CHECK(cusolverDnCheevj(
      handle,
      jobz,
      uplo,
      n,
      reinterpret_cast<cuComplex*>(A),
      lda,
      W,
      reinterpret_cast<cuComplex*>(work),
      lwork,
      info,
      params));
}

template <>
void syevj<c10::complex<double>, double>(
    cusolverDnHandle_t handle,
    cusolverEigMode_t jobz,
    cublasFillMode_t uplo,
    int n,
    c10::complex<double>* A,
    int lda,
    double* W,
    c10::complex<double>* work,
    int lwork,
    int* info,
    syevjInfo_t params) {
  TORCH_CUSOLVER_CHECK(cusolverDnZheevj(
      handle,
      jobz,
      uplo,
      n,
      reinterpret_cast<cuDoubleComplex*>(A),
      lda,
      W,
      reinterpret_cast<cuDoubleComplex*>(work),
      lwork,
      info,
      params));
}

template <>
void syevjBatched_bufferSize<float>(
    cusolverDnHandle_t handle,
    cusolverEigMode_t jobz,
    cublasFillMode_t uplo,
    int n,
    const float* A,
    int lda,
    const float* W,
    int* lwork,
    syevjInfo_t params,
    int batchsize) {
  TORCH_CUSOLVER_CHECK(cusolverDnSsyevjBatched_bufferSize(
      handle, jobz, uplo, n, A, lda, W, lwork, params, batchsize));
}

template <>
void syevjBatched_bufferSize<double>(
    cusolverDnHandle_t handle,
    cusolverEigMode_t jobz,
    cublasFillMode_t uplo,
    int n,
    const double* A,
    int lda,
    const double* W,
    int* lwork,
    syevjInfo_t params,
    int batchsize) {
  TORCH_CUSOLVER_CHECK(cusolverDnDsyevjBatched_bufferSize(
      handle, jobz, uplo, n, A, lda, W, lwork, params, batchsize));
}

template <>
void syevjBatched_bufferSize<c10::complex<float>, float>(
    cusolverDnHandle_t handle,
    cusolverEigMode_t jobz,
    cublasFillMode_t uplo,
    int n,
    const c10::complex<float>* A,
    int lda,
    const float* W,
    int* lwork,
    syevjInfo_t params,
    int batchsize) {
  TORCH_CUSOLVER_CHECK(cusolverDnCheevjBatched_bufferSize(
      handle,
      jobz,
      uplo,
      n,
      reinterpret_cast<const cuComplex*>(A),
      lda,
      W,
      lwork,
      params,
      batchsize));
}

template <>
void syevjBatched_bufferSize<c10::complex<double>, double>(
    cusolverDnHandle_t handle,
    cusolverEigMode_t jobz,
    cublasFillMode_t uplo,
    int n,
    const c10::complex<double>* A,
    int lda,
    const double* W,
    int* lwork,
    syevjInfo_t params,
    int batchsize) {
  TORCH_CUSOLVER_CHECK(cusolverDnZheevjBatched_bufferSize(
      handle,
      jobz,
      uplo,
      n,
      reinterpret_cast<const cuDoubleComplex*>(A),
      lda,
      W,
      lwork,
      params,
      batchsize));
}

template <>
void syevjBatched<float>(
    cusolverDnHandle_t handle,
    cusolverEigMode_t jobz,
    cublasFillMode_t uplo,
    int n,
    float* A,
    int lda,
    float* W,
    float* work,
    int lwork,
    int* info,
    syevjInfo_t params,
    int batchsize) {
  TORCH_CUSOLVER_CHECK(cusolverDnSsyevjBatched(
      handle, jobz, uplo, n, A, lda, W, work, lwork, info, params, batchsize));
}

template <>
void syevjBatched<double>(
    cusolverDnHandle_t handle,
    cusolverEigMode_t jobz,
    cublasFillMode_t uplo,
    int n,
    double* A,
    int lda,
    double* W,
    double* work,
    int lwork,
    int* info,
    syevjInfo_t params,
    int batchsize) {
  TORCH_CUSOLVER_CHECK(cusolverDnDsyevjBatched(
      handle, jobz, uplo, n, A, lda, W, work, lwork, info, params, batchsize));
}

template <>
void syevjBatched<c10::complex<float>, float>(
    cusolverDnHandle_t handle,
    cusolverEigMode_t jobz,
    cublasFillMode_t uplo,
    int n,
    c10::complex<float>* A,
    int lda,
    float* W,
    c10::complex<float>* work,
    int lwork,
    int* info,
    syevjInfo_t params,
    int batchsize) {
  TORCH_CUSOLVER_CHECK(cusolverDnCheevjBatched(
      handle,
      jobz,
      uplo,
      n,
      reinterpret_cast<cuComplex*>(A),
      lda,
      W,
      reinterpret_cast<cuComplex*>(work),
      lwork,
      info,
      params,
      batchsize));
}

template <>
void syevjBatched<c10::complex<double>, double>(
    cusolverDnHandle_t handle,
    cusolverEigMode_t jobz,
    cublasFillMode_t uplo,
    int n,
    c10::complex<double>* A,
    int lda,
    double* W,
    c10::complex<double>* work,
    int lwork,
    int* info,
    syevjInfo_t params,
    int batchsize) {
  TORCH_CUSOLVER_CHECK(cusolverDnZheevjBatched(
      handle,
      jobz,
      uplo,
      n,
      reinterpret_cast<cuDoubleComplex*>(A),
      lda,
      W,
      reinterpret_cast<cuDoubleComplex*>(work),
      lwork,
      info,
      params,
      batchsize));
}

#ifdef USE_CUSOLVER_64_BIT
template <>
void xsyevd_bufferSize<float>(
    cusolverDnHandle_t handle,
    cusolverDnParams_t params,
    cusolverEigMode_t jobz,
    cublasFillMode_t uplo,
    int64_t n,
    const float* A,
    int64_t lda,
    const float* W,
    size_t* workspaceInBytesOnDevice,
    size_t* workspaceInBytesOnHost) {
  TORCH_CUSOLVER_CHECK(cusolverDnXsyevd_bufferSize(
      handle,
      params,
      jobz,
      uplo,
      n,
      CUDA_R_32F,
      reinterpret_cast<const void*>(A),
      lda,
      CUDA_R_32F,
      reinterpret_cast<const void*>(W),
      CUDA_R_32F,
      workspaceInBytesOnDevice,
      workspaceInBytesOnHost));
}

template <>
void xsyevd_bufferSize<double>(
    cusolverDnHandle_t handle,
    cusolverDnParams_t params,
    cusolverEigMode_t jobz,
    cublasFillMode_t uplo,
    int64_t n,
    const double* A,
    int64_t lda,
    const double* W,
    size_t* workspaceInBytesOnDevice,
    size_t* workspaceInBytesOnHost) {
  TORCH_CUSOLVER_CHECK(cusolverDnXsyevd_bufferSize(
      handle,
      params,
      jobz,
      uplo,
      n,
      CUDA_R_64F,
      reinterpret_cast<const void*>(A),
      lda,
      CUDA_R_64F,
      reinterpret_cast<const void*>(W),
      CUDA_R_64F,
      workspaceInBytesOnDevice,
      workspaceInBytesOnHost));
}

template <>
void xsyevd_bufferSize<c10::complex<float>, float>(
    cusolverDnHandle_t handle,
    cusolverDnParams_t params,
    cusolverEigMode_t jobz,
    cublasFillMode_t uplo,
    int64_t n,
    const c10::complex<float>* A,
    int64_t lda,
    const float* W,
    size_t* workspaceInBytesOnDevice,
    size_t* workspaceInBytesOnHost) {
  TORCH_CUSOLVER_CHECK(cusolverDnXsyevd_bufferSize(
      handle,
      params,
      jobz,
      uplo,
      n,
      CUDA_C_32F,
      reinterpret_cast<const void*>(A),
      lda,
      CUDA_R_32F,
      reinterpret_cast<const void*>(W),
      CUDA_C_32F,
      workspaceInBytesOnDevice,
      workspaceInBytesOnHost));
}

template <>
void xsyevd_bufferSize<c10::complex<double>, double>(
    cusolverDnHandle_t handle,
    cusolverDnParams_t params,
    cusolverEigMode_t jobz,
    cublasFillMode_t uplo,
    int64_t n,
    const c10::complex<double>* A,
    int64_t lda,
    const double* W,
    size_t* workspaceInBytesOnDevice,
    size_t* workspaceInBytesOnHost) {
  TORCH_CUSOLVER_CHECK(cusolverDnXsyevd_bufferSize(
      handle,
      params,
      jobz,
      uplo,
      n,
      CUDA_C_64F,
      reinterpret_cast<const void*>(A),
      lda,
      CUDA_R_64F,
      reinterpret_cast<const void*>(W),
      CUDA_C_64F,
      workspaceInBytesOnDevice,
      workspaceInBytesOnHost));
}

template <>
void xsyevd<float>(
    cusolverDnHandle_t handle,
    cusolverDnParams_t params,
    cusolverEigMode_t jobz,
    cublasFillMode_t uplo,
    int64_t n,
    float* A,
    int64_t lda,
    float* W,
    float* bufferOnDevice,
    size_t workspaceInBytesOnDevice,
    float* bufferOnHost,
    size_t workspaceInBytesOnHost,
    int* info) {
  TORCH_CUSOLVER_CHECK(cusolverDnXsyevd(
      handle,
      params,
      jobz,
      uplo,
      n,
      CUDA_R_32F,
      reinterpret_cast<void*>(A),
      lda,
      CUDA_R_32F,
      reinterpret_cast<void*>(W),
      CUDA_R_32F,
      reinterpret_cast<void*>(bufferOnDevice),
      workspaceInBytesOnDevice,
      reinterpret_cast<void*>(bufferOnHost),
      workspaceInBytesOnHost,
      info));
}

template <>
void xsyevd<double>(
    cusolverDnHandle_t handle,
    cusolverDnParams_t params,
    cusolverEigMode_t jobz,
    cublasFillMode_t uplo,
    int64_t n,
    double* A,
    int64_t lda,
    double* W,
    double* bufferOnDevice,
    size_t workspaceInBytesOnDevice,
    double* bufferOnHost,
    size_t workspaceInBytesOnHost,
    int* info) {
  TORCH_CUSOLVER_CHECK(cusolverDnXsyevd(
      handle,
      params,
      jobz,
      uplo,
      n,
      CUDA_R_64F,
      reinterpret_cast<void*>(A),
      lda,
      CUDA_R_64F,
      reinterpret_cast<void*>(W),
      CUDA_R_64F,
      reinterpret_cast<void*>(bufferOnDevice),
      workspaceInBytesOnDevice,
      reinterpret_cast<void*>(bufferOnHost),
      workspaceInBytesOnHost,
      info));
}

template <>
void xsyevd<c10::complex<float>, float>(
    cusolverDnHandle_t handle,
    cusolverDnParams_t params,
    cusolverEigMode_t jobz,
    cublasFillMode_t uplo,
    int64_t n,
    c10::complex<float>* A,
    int64_t lda,
    float* W,
    c10::complex<float>* bufferOnDevice,
    size_t workspaceInBytesOnDevice,
    c10::complex<float>* bufferOnHost,
    size_t workspaceInBytesOnHost,
    int* info) {
  TORCH_CUSOLVER_CHECK(cusolverDnXsyevd(
      handle,
      params,
      jobz,
      uplo,
      n,
      CUDA_C_32F,
      reinterpret_cast<void*>(A),
      lda,
      CUDA_R_32F,
      reinterpret_cast<void*>(W),
      CUDA_C_32F,
      reinterpret_cast<void*>(bufferOnDevice),
      workspaceInBytesOnDevice,
      reinterpret_cast<void*>(bufferOnHost),
      workspaceInBytesOnHost,
      info));
}

template <>
void xsyevd<c10::complex<double>, double>(
    cusolverDnHandle_t handle,
    cusolverDnParams_t params,
    cusolverEigMode_t jobz,
    cublasFillMode_t uplo,
    int64_t n,
    c10::complex<double>* A,
    int64_t lda,
    double* W,
    c10::complex<double>* bufferOnDevice,
    size_t workspaceInBytesOnDevice,
    c10::complex<double>* bufferOnHost,
    size_t workspaceInBytesOnHost,
    int* info) {
  TORCH_CUSOLVER_CHECK(cusolverDnXsyevd(
      handle,
      params,
      jobz,
      uplo,
      n,
      CUDA_C_64F,
      reinterpret_cast<void*>(A),
      lda,
      CUDA_R_64F,
      reinterpret_cast<void*>(W),
      CUDA_C_64F,
      reinterpret_cast<void*>(bufferOnDevice),
      workspaceInBytesOnDevice,
      reinterpret_cast<void*>(bufferOnHost),
      workspaceInBytesOnHost,
      info));
}
#endif // USE_CUSOLVER_64_BIT

} // namespace solver
} // namespace cuda
} // namespace at

#endif // CUDART_VERSION
