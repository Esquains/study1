/*
The following source file implements a sparse linear operator using cusparseLt
*/

#include <ATen/Functions.h>
#include <c10/core/ScalarType.h>
#include <c10/util/Half.h>
#include <torch/custom_class.h>
#include <ATen/cuda/CUDAContext.h>
#include <ATen/cuda/CUDADataType.h>
#include <ATen/cuda/CUDAUtils.h>
#include <iostream>

#include <iostream>

#include <cutlass/cutlass.h>
#include <cutlass/gemm/device/gemm_sparse.h>
#include <cutlass/util/host_tensor.h>
#include <cutlass/util/reference/host/gemm.h>
#include <cutlass/util/host_reorder.h>
#include <cutlass/util/host_uncompress.h>
#include <cutlass/util/reference/host/tensor_compare.h>
#include <cutlass/util/reference/host/tensor_copy.h>
#include <cutlass/util/reference/host/tensor_fill.h>
#include <cutlass/util/tensor_view_io.h>
#include <cuda_runtime.h>

#include <typeinfo>
#include <limits>

#define CUTLASS_CHECK(status)                                                                    \
  {                                                                                              \
    cutlass::Status error = status;                                                              \
    if (error != cutlass::Status::kSuccess) {                                                    \
      std::cerr << "Got cutlass error: " << cutlassGetStatusString(error) << " at: " << __LINE__ \
                << std::endl;                                                                    \
      exit(EXIT_FAILURE);                                                                        \
    }                                                                                            \
  }

#define CUDA_CHECK(status)                                              \
  {                                                                     \
    cudaError_t error = status;                                         \
    if (error != cudaSuccess) {                                         \
      std::cerr << "Got bad cuda status: " << cudaGetErrorString(error) \
                << " at line: " << __LINE__ << std::endl;               \
      exit(EXIT_FAILURE);                                               \
    }                                                                   \
  }




/**
Please check example 07, 08 and 17 for the basics of dense tensor op gemm kernels.  NVIDIA Ampere
architecture also supports structured sparse tensor op for tf32, fp16, int8 and int4.

Sparse GEMM kernels needs to takes an additional E matrix which stores the meta data.  The format of
meta data is different for every data types.   CUTLASS templates can automatically infer it based on
input A and B.  Check code below.

Moreover, matrix E needs to be preprocessed so that it can use ldmatrix to load into the registers
efficiently.
*/


// The code section below describes datatype for input, output matrices and computation between
// elements in input matrices, which will all be used as template parameters for cutlass::gemm::device::SparseGemm
using ElementAccumulator = float;                 // <- data type of accumulator
using ElementComputeEpilogue = ElementAccumulator;  // <- data type of epilogue operations
using ElementInputA = cutlass::half_t;             // <- data type of elements in input matrix A
using ElementInputB = cutlass::half_t;             // <- data type of elements in input matrix B
using ElementOutput = cutlass::half_t;                      // <- data type of elements in output matrix D

// The code section below describes matrix layout of input and output matrices. Row Major for
// Matrix A, Column Major for Matrix B and Row Major for Matrix C
using LayoutInputA = cutlass::layout::RowMajor;
using LayoutInputB = cutlass::layout::RowMajor;
using LayoutOutput = cutlass::layout::RowMajor;

// This code section describes whether you want to use tensor cores or regular SIMT cores on GPU SM
using MMAOp = cutlass::arch::OpClassTensorOp;

// This code section describes CUDA SM architecture number
using SmArch = cutlass::arch::Sm80;

// This code section describes the tile size a thread block will compute
// What should we these be for fp16 pyspeech shapse?
// current settings are from fp16 cutlass sparse gemm unit test
using ShapeMMAThreadBlock =
    cutlass::gemm::GemmShape<128, 256, 64>;  // <- threadblock tile M = 128, N = 128, K = 256
// This code section describes tile size a warp will compute
// using ShapeMMAWarp = cutlass::gemm::GemmShape<64, 64, 64>;  // <- warp tile M = 64, N = 64, K = 256
using ShapeMMAWarp = cutlass::gemm::GemmShape<64, 64, 64>;  // <- warp tile M = 64, N = 64, K = 256
// This code section describes the size of MMA op
using ShapeMMAOp = cutlass::gemm::GemmShape<16, 8, 32>;  // <- MMA Op tile M = 16, N = 8, K = 128

// This code section describes how threadblocks are scheduled on GPU
using SwizzleThreadBlock = cutlass::gemm::threadblock::GemmIdentityThreadblockSwizzle<>;  // <- ??

// This code section describes the epilogue part of the kernel
using EpilogueOp = cutlass::epilogue::thread::LinearCombination<
    ElementOutput,                                     // <- data type of output matrix
    128 / cutlass::sizeof_bits<ElementOutput>::value,  // <- the number of elements per vectorized
                                                       // memory access. For a byte, it's 16
                                                       // elements. This becomes the vector width of
                                                       // math instructions in the epilogue too
    ElementAccumulator,                                // <- data type of accumulator
    ElementAccumulator>;  // <- data type for alpha/beta in linear combination function

// Number of pipelines you want to use
constexpr int NumStages = 3;

using Gemm = cutlass::gemm::device::SparseGemm<ElementInputA,
                                               LayoutInputA,
                                               ElementInputB,
                                               LayoutInputB,
                                               ElementOutput,
                                               LayoutOutput,
                                               ElementAccumulator,
                                               MMAOp,
                                               SmArch,
                                               ShapeMMAThreadBlock,
                                               ShapeMMAWarp,
                                               ShapeMMAOp,
                                               EpilogueOp,
                                               SwizzleThreadBlock,
                                               NumStages>;

// Data type and layout of meta data matrix E can be inferred from template Gemm.
using ElementInputE = typename Gemm::ElementE;
using LayoutInputE = cutlass::layout::RowMajor;
using ReorderedLayoutInputE = typename Gemm::LayoutE;

// Below property is defined in include/cutlass/arch/sp_mma_sm80.h
// 50% Sparsity on Ampere
constexpr int kSparse = Gemm::kSparse;
// How many elements of A are covered per ElementE
constexpr int kElementsPerElementE = Gemm::kElementsPerElementE;
// The size of individual meta data
constexpr int kMetaSizeInBits = Gemm::kMetaSizeInBits;

int run(
    at::Tensor tensor_a,
    at::Tensor tensor_b,
    at::Tensor tensor_c,
    at::Tensor tensor_d) {
  // tensor a is m x (k // kSparse); tensor b is k x n
  const int length_m = tensor_a.size(0);
  const int length_k = tensor_b.size(0);
  const int length_n = tensor_b.size(1);

  std::cout << "length_m: " << length_m << std::endl;
  std::cout << "length_n: " << length_n << std::endl;
  std::cout << "length_k: " << length_k << std::endl;

  std::cout << "kSparse: " << kSparse << std::endl;
  std::cout << "kElementsPerElementE: " << kElementsPerElementE << std::endl;
//  std::cout << "ElementInputE: " << ElementInputE << std::endl;
  ElementInputE asdf;
  std::cout << "ElementInputE: " << typeid(asdf).name() << std::endl;
  std::cout << "std::numeric_limits<ElementInputE>: " << std::numeric_limits<ElementInputE>::digits << std::endl;
  TORCH_CHECK(
      tensor_b.size(0) % kSparse == 0,
      "Expected tensor_b.size(0) of value ",
      tensor_b.size(0),
      " to be evenly divisible by ",
      kSparse,
      " but got.");
  TORCH_CHECK(
      tensor_a.size(1) * kSparse == tensor_b.size(0),
      "Expected tensor_a.size(1) of value ",
      tensor_a.size(1),
      " to match tensor_b.size(0) of value ",
      tensor_b.size(0),
      " to match after being multiplied by ",
      kSparse);

  TORCH_CHECK(tensor_c.size(0) == tensor_a.size(0));
  TORCH_CHECK(tensor_d.size(0) == tensor_a.size(0));

  TORCH_CHECK(tensor_c.size(1) == tensor_b.size(1));
  TORCH_CHECK(tensor_d.size(1) == tensor_b.size(1));

  // Create a tuple of problem size for matrix multiplication
  cutlass::gemm::GemmCoord problem_size(length_m, length_n, length_k);

  // TODO:
  // Feed tensor_e as CPU int16 Tensor.
  // Try various valid 2:4 meta configurations
  // 0x4 0100
  // 0x8 1000
  // 0x9 1001
  // 0xc 1100
  // 0xd 1101
  // 0xe 1110

  // Create matrix E with dimensions M x (K / 2 / kElementsPerElementE). This one is used by reference computing.
  cutlass::HostTensor<ElementInputE, LayoutInputE> tensor_e(
      cutlass::make_Coord(problem_size.m(), problem_size.k() / kSparse / kElementsPerElementE));
  // Same size as the above.  The above one needs to be reordered and stored in this one.
  cutlass::HostTensor<ElementInputE, ReorderedLayoutInputE> tensor_e_reordered(
      cutlass::make_Coord(problem_size.m(), problem_size.k() / kSparse / kElementsPerElementE));

  cutlass::reference::host::TensorFillRandomSparseMeta(
      tensor_e.host_view(),
      1,
      kMetaSizeInBits);   // <- Fill matrix E on host with uniform-distribution random meta data

  // Reorder the meta data matrix so that we can use ldmatrix to load them to tensor core
  // instructions.
  cutlass::reorder_meta(tensor_e_reordered.host_ref(), tensor_e.host_ref(),
                        {problem_size.m(), problem_size.n(),
                         problem_size.k() / kSparse / kElementsPerElementE});

  tensor_e_reordered.sync_device();

  // Initialize alpha and beta for dot product computation
  ElementComputeEpilogue alpha = ElementComputeEpilogue(1);
  ElementComputeEpilogue beta = ElementComputeEpilogue(0);

  LayoutInputA layout_a;
  LayoutInputB layout_b;
  LayoutOutput layout_c;
  LayoutOutput layout_d;

  // Split K dimension into 1 partitions
  int split_k_slices = 1;
  auto tensor_a_device_ref = cutlass::TensorRef<cutlass::half_t, LayoutInputA>((cutlass::half_t*)tensor_a.data_ptr<at::Half>(), layout_a);
  auto tensor_b_device_ref = cutlass::TensorRef<cutlass::half_t, LayoutInputB>((cutlass::half_t*)tensor_b.data_ptr<at::Half>(), layout_b);
  auto tensor_c_device_ref = cutlass::TensorRef<cutlass::half_t, LayoutOutput>((cutlass::half_t*)tensor_c.data_ptr<at::Half>(), layout_c);
  auto tensor_d_device_ref = cutlass::TensorRef<cutlass::half_t, LayoutOutput>((cutlass::half_t*)tensor_d.data_ptr<at::Half>(), layout_d);

  // Create a tuple of gemm kernel arguments. This is later passed as arguments to launch
  // instantiated CUTLASS kernel
  typename Gemm::Arguments arguments{problem_size,  // <- problem size of matrix multiplication
                                     tensor_a_device_ref,  // <- reference to matrix A on device
                                     tensor_b_device_ref,  // <- reference to matrix B on device
                                     tensor_c_device_ref,  // <- reference to matrix C on device
                                     tensor_d_device_ref,  // <- reference to matrix D on device
                                     tensor_e_reordered.device_ref(),  // <- reference to matrix E on device
                                     {alpha, beta},          // <- tuple of alpha and beta
                                     split_k_slices};        // <- k-dimension split factor

  // Using the arguments, query for extra workspace required for matrix multiplication computation
  size_t workspace_size = Gemm::get_workspace_size(arguments);

  // Allocate workspace memory
  cutlass::device_memory::allocation<uint8_t> workspace(workspace_size);

  // Instantiate CUTLASS kernel depending on templates
  Gemm gemm_op;

  // Check the problem size is supported or not
  cutlass::Status status = gemm_op.can_implement(arguments);
  CUTLASS_CHECK(status);

  // Initialize CUTLASS kernel with arguments and workspace pointer
  status = gemm_op.initialize(arguments, workspace.get());
  CUTLASS_CHECK(status);

  // Launch initialized CUTLASS kernel
  status = gemm_op();
  CUTLASS_CHECK(status);
  return 0;

}

namespace at {
namespace native {

// TODO: Pull back in device and cuda version constraints.
Tensor _cusparselt_linear(const Tensor& sparse, const Tensor& dense) {
  auto result = sparse.new_empty({sparse.size(0), dense.size(1)}).fill_(1);
  auto init = sparse.new_empty({sparse.size(0), dense.size(1)}).fill_(2);
  run(sparse, dense, init, result);
  return result;
}

}
}
