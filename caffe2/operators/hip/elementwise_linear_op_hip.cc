/**
 * Copyright (c) 2016-present, Facebook, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <assert.h>
#include "hip/hip_runtime.h"
#include "elementwise_linear_op.h"
#include "caffe2/core/context_hip.h"
#include "caffe2/operators/operator_fallback_hip.h"
#include <cub/block/block_reduce.cuh>

namespace caffe2 {

namespace {
__global__ void ElementwiseLinearKernel(const int N,
                                        const int D,
                                        const float* X_data,
                                        const float* a_data,
                                        const float* b_data,
                                        float* Y_data)
{
    HIP_1D_KERNEL_LOOP(i, N * D)
    {
        int d     = i % D;
        Y_data[i] = X_data[i] * a_data[d] + b_data[d];
    }
}

__global__ void ElementwiseLinearGradientKernel(const int N,
                                                const int D,
                                                const float* g_o_data,
                                                const float* X_data,
                                                const float* a_data,
                                                float* g_X_data,
                                                float* g_a_data,
                                                float* g_b_data)
{
    int d = hipBlockIdx_x; // One block per D

    float g_a_sum = 0;
    float g_b_sum = 0;
    for(int n = hipThreadIdx_x; n < N; n += hipBlockDim_x)
    {
        const float gox     = g_o_data[n * D + d];
        g_X_data[n * D + d] = gox * a_data[d];
        g_a_sum += gox * X_data[n * D + d];
        g_b_sum += gox;
    }
    typedef cub::BlockReduce<float, CAFFE_HIP_NUM_THREADS> BlockReduce;
    __shared__ typename BlockReduce::TempStorage temp_storage;

    float g_a_sum_tot = BlockReduce(temp_storage).Sum(g_a_sum);
    __syncthreads();
    float g_b_sum_tot = BlockReduce(temp_storage).Sum(g_b_sum);

    if(hipThreadIdx_x == 0)
    {
        g_a_data[d] = g_a_sum_tot;
        g_b_data[d] = g_b_sum_tot;
    }
}

} // namespace

template <>
bool ElementwiseLinearOp<float, HIPContext>::RunOnDevice()
{
    const auto& X = Input(0);
    const auto& a = Input(1);
    const auto& b = Input(2);
    auto* Y       = Output(0);

    const auto canonical_axis = X.canonical_axis_index(axis_);
    const int N               = X.size_to_dim(canonical_axis);
    const int D               = X.size_from_dim(canonical_axis);

    CAFFE_ENFORCE_EQ(a.ndim(), 1, a.ndim());
    CAFFE_ENFORCE_EQ(a.dim(0), D, a.ndim());
    CAFFE_ENFORCE_EQ(b.ndim(), 1, b.ndim());
    CAFFE_ENFORCE_EQ(b.dim(0), D, b.ndim());

    Y->ResizeLike(X);

    hipLaunchKernelGGL((ElementwiseLinearKernel),
                       dim3(CAFFE_GET_BLOCKS(N * D)),
                       dim3(CAFFE_HIP_NUM_THREADS),
                       0,
                       context_.hip_stream(),
                       N,
                       D,
                       X.data<float>(),
                       a.data<float>(),
                       b.data<float>(),
                       Y->mutable_data<float>());
    return true;
}

template <>
bool ElementwiseLinearGradientOp<float, HIPContext>::RunOnDevice()
{
    const auto& g_o = Input(0);
    const auto& X   = Input(1);
    const auto& a   = Input(2);

    const auto canonical_axis = X.canonical_axis_index(axis_);
    const int N               = X.size_to_dim(canonical_axis);
    const int D               = X.size_from_dim(canonical_axis);

    CAFFE_ENFORCE_EQ(a.ndim(), 1, a.ndim());
    CAFFE_ENFORCE_EQ(a.dim(0), D, a.ndim());

    auto* g_X = Output(0);
    auto* g_a = Output(1);
    auto* g_b = Output(2);
    g_X->ResizeLike(X);
    g_a->ResizeLike(a);
    g_b->ResizeLike(a);

    float* g_a_data = g_a->mutable_data<float>();
    float* g_b_data = g_b->mutable_data<float>();

    hipLaunchKernelGGL((ElementwiseLinearGradientKernel),
                       dim3(D),
                       dim3(CAFFE_HIP_NUM_THREADS),
                       0,
                       context_.hip_stream(),
                       N,
                       D,
                       g_o.data<float>(),
                       X.data<float>(),
                       a.data<float>(),
                       g_X->mutable_data<float>(),
                       g_a_data,
                       g_b_data);
    return true;
}

REGISTER_HIP_OPERATOR(ElementwiseLinear, ElementwiseLinearOp<float, HIPContext>);
REGISTER_HIP_OPERATOR(ElementwiseLinearGradient, ElementwiseLinearGradientOp<float, HIPContext>);

} // namespace caffe2
