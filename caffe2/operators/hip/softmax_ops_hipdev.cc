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

#include <cfloat>
#include <cub/block/block_reduce.cuh>
#include "hip/hip_runtime.h"
#include "caffe2/core/context_hip.h"
#include "softmax_op.h"
#include "softmax_with_loss_op.h"
#include "spatial_softmax_with_loss_op.h"

namespace caffe2 {

namespace {

__global__ void LabelCrossEntropyKernel(const int N,
                                        const int D,
                                        const float* logPdata,
                                        const int* labeldata,
                                        const float* weights,
                                        float* Ydata)
{
    HIP_1D_KERNEL_LOOP(i, N)
    {
        HIP_KERNEL_ASSERT(labeldata[i] >= 0 && labeldata[i] < D);
        float weight = weights ? weights[i] : 1.0;
        Ydata[i]     = -logPdata[i * D + labeldata[i]] * weight;
    }
}

__global__ void LabelCrossEntropyGradientKernel(
    const int N, const int D, const float* Pdata, const int* labeldata, float* dXdata)
{
    HIP_1D_KERNEL_LOOP(i, N)
    {
        int idx     = i * D + labeldata[i];
        dXdata[idx] = Pdata[idx] - 1.;
    }
}

__global__ void LabelCrossEntropyGradientKernelWeighted(const int N,
                                                        const int D,
                                                        const float* Pdata,
                                                        const int* labeldata,
                                                        float* dXdata,
                                                        const float* weights)
{
    HIP_1D_KERNEL_LOOP(i, N * D)
    {
        int row      = i / D;
        int d        = i % D;
        float val    = Pdata[i] - 1.0 * (d == labeldata[row]);
        float weight = weights[row];
        dXdata[i]    = val * weight;
    }
}

__global__ void ProbCrossEntropyKernel(const int N,
                                       const int D,
                                       const float* Pdata,
                                       const float* labeldata,
                                       const float* weights,
                                       float* Ydata)
{
    typedef cub::BlockReduce<float, CAFFE_HIP_NUM_THREADS> BlockReduce;
    __shared__ typename BlockReduce::TempStorage temp_storage;

    for(int i = hipBlockIdx_x; i < N; i += hipGridDim_x)
    {
        float weight     = weights ? weights[i] : 1.0;
        float sum        = 0.0;
        float total_prob = 0.0;
        for(int j = hipThreadIdx_x; j < D; j += hipBlockDim_x)
        {
            int idx = i * D + j;
            HIP_KERNEL_ASSERT(labeldata[idx] >= 0);
            total_prob += labeldata[idx];
            sum += -logf(fmaxf(Pdata[idx], FLT_MIN)) * labeldata[idx] * weight;
        }
        float tot = BlockReduce(temp_storage).Sum(sum);
        __syncthreads();
        float total_prob_sum = BlockReduce(temp_storage).Sum(total_prob);
        if(hipThreadIdx_x == 0)
        {
            Ydata[i] = tot;
            // Sanity check
            HIP_KERNEL_ASSERT(fabsf(1.0 - total_prob_sum) < 1e-5f);
        }
        __syncthreads();
    }
}

__global__ void ProbCrossEntropyGradientKernel(const int N,
                                               const int D,
                                               const float* Pdata,
                                               const float* labeldata,
                                               float* dXdata,
                                               const float* weights)
{
    if(weights == NULL)
    {
        HIP_1D_KERNEL_LOOP(idx, N * D) { dXdata[idx] = Pdata[idx] - labeldata[idx]; }
    }
    else
    {
        HIP_1D_KERNEL_LOOP(idx, N * D)
        {
            dXdata[idx] = (Pdata[idx] - labeldata[idx]) * weights[idx / D];
        }
    }
}

__global__ void SpatialSoftmaxKernel(
    const int num, const int D, const int W, const int H, const float* Xdata, float* Pdata)
{
    HIP_1D_KERNEL_LOOP(index, num * W * H)
    {
        int x = index % W;
        int y = (index / W) % H;
        int i = index / W / H;

        // Subtract max on each cell for numerical reasons
        float max_val = -FLT_MAX;
        for(int c = 0; c < D; ++c)
        {
            int idx = i * (H * W * D) + c * (H * W) + y * W + x;
            max_val = fmaxf(max_val, Xdata[idx]);
        }

        // Exponentiate
        float expsum = 0.0f;
        for(int c = 0; c < D; ++c)
        {
            int idx    = i * (H * W * D) + c * (H * W) + y * W + x;
            float expx = expf(Xdata[idx] - max_val);
            Pdata[idx] = expx;
            expsum += expx;
        }

        // Normalize
        for(int c = 0; c < D; ++c)
        {
            int idx = i * (H * W * D) + c * (H * W) + y * W + x;
            Pdata[idx] /= expsum;
        }
    }
}

#define DONTCARE (-1)

__global__ void SpatialCrossEntropyLossKernel(const int N,
                                              const int D,
                                              const int W,
                                              const int H,
                                              const float* Pdata,
                                              const int* label_data,
                                              const float* weights,
                                              float* loss_data,
                                              float* weight_data)
{
    HIP_1D_KERNEL_LOOP(index, N * W * H)
    {
        int x           = index % W;
        int y           = (index / W) % H;
        int i           = index / W / H;
        const int label = static_cast<int>(label_data[index]);

        if(label != DONTCARE)
        {
            HIP_KERNEL_ASSERT(label >= 0 && label < D);
            float weight = (weights == NULL ? 1.0 : weights[index]);
            loss_data[index] =
                -logf(fmaxf(Pdata[i * W * H * D + label * W * H + y * W + x], 1e-20f)) * weight;
            weight_data[index] = weight;
        }
        else
        {
            loss_data[index]   = 0;
            weight_data[index] = 0;
        }
    }
}

__global__ void SpatialSoftmaxLossGradientKernel(const int N,
                                                 const int D,
                                                 const int W,
                                                 const int H,
                                                 const int* label_data,
                                                 const float* weights,
                                                 float* dX_data,
                                                 float* weights_)
{
    HIP_1D_KERNEL_LOOP(index, N * W * H)
    {
        int x           = index % W;
        int y           = (index / W) % H;
        int i           = index / W / H;
        const int label = static_cast<int>(label_data[index]);

        if(label != DONTCARE)
        {
            int data_idx = i * (H * W * D) + label * (H * W) + y * W + x;
            dX_data[data_idx] -= 1.0;
            if(weights != NULL)
            {
                float weight = weights[index];
                for(int c = 0; c < D; ++c)
                {
                    int data_idx = i * (H * W * D) + c * (H * W) + y * W + x;
                    dX_data[data_idx] *= weight;
                }
                weights_[index] = weight;
            }
            else
            {
                weights_[index] = 1.0;
            }
        }
        else
        {
            // Ignore-label, so set all gradients for this positions
            // tp zero
            for(int c = 0; c < D; ++c)
            {
                int data_idx      = i * (H * W * D) + c * (H * W) + y * W + x;
                dX_data[data_idx] = 0.0;
            }
            weights_[index] = 0.0;
        }
    }
}

__global__ void SoftmaxNormalizeLogsKernel(const int nthreads,
                                           const int D,
                                           const float* logits,
                                           const float* rowmax,
                                           const float* scales,
                                           float* out_log)
{
    HIP_1D_KERNEL_LOOP(index, nthreads)
    {
        int n          = index / D;
        out_log[index] = logits[index] - rowmax[n] - logf(fmaxf(scales[n], FLT_MIN));
    }
}

__global__ void SoftmaxNormalizeKernel(
    const int nthreads, const int D, const float* probs, const float* scales, float* out)
{
    HIP_1D_KERNEL_LOOP(index, nthreads)
    {
        int n      = index / D;
        out[index] = probs[index] / scales[n];
    }
}

void Softmax(const int N,
             const int D,
             const float* logits,
             const float* sum_multiplier,
             float* scales,
             float* rowmax,
             float* probs,
             bool log_softmax,
             HIPContext* context)
{
    const int size = N * D;

    math::RowwiseMax<float, HIPContext>(N, D, logits, rowmax, context);
    // Put the intermediate result X - max(X) into Y
    context->Copy<float, HIPContext, HIPContext>(size, logits, probs);
    // Subtract the scale
    math::Gemm<float, HIPContext>(
        CblasNoTrans, CblasNoTrans, N, D, 1, -1, rowmax, sum_multiplier, 1, probs, context);
    // Exponentiation
    math::Exp<float, HIPContext>(size, probs, probs, context);
    // Sum exponentiated values
    math::Gemv<float, HIPContext>(CblasNoTrans, N, D, 1, probs, sum_multiplier, 0, scales, context);
    // Normalize
    if(!log_softmax)
    {
        hipLaunchKernelGGL((SoftmaxNormalizeKernel),
                           dim3(CAFFE_GET_BLOCKS(size)),
                           dim3(CAFFE_HIP_NUM_THREADS),
                           0,
                           context->hip_stream(),
                           size,
                           D,
                           probs,
                           scales,
                           probs);
    }
    else
    {
        hipLaunchKernelGGL((SoftmaxNormalizeLogsKernel),
                           dim3(CAFFE_GET_BLOCKS(size)),
                           dim3(CAFFE_HIP_NUM_THREADS),
                           0,
                           context->hip_stream(),
                           size,
                           D,
                           logits,
                           rowmax,
                           scales,
                           probs);
    }
}

} // namespace

template <>
bool SoftmaxWithLossOp<float, HIPContext>::RunOnDevice()
{
    auto& X              = Input(0);  // Logits
    auto& T              = Input(1);  // Labels / targets
    auto* P              = Output(0); // Probabilities from softmax
    auto* avg_loss       = Output(1); // Average loss
    const float* weights = (InputSize() > 2 ? Input(2).data<float>() : NULL);

    const auto canonical_axis = X.canonical_axis_index(axis_);
    int N, D;
    N = X.size_to_dim(canonical_axis); // batch size
    D = X.size_from_dim(canonical_axis);
    P->ResizeLike(X);
    total_weight_ptr_.Resize(1);

    if(label_prob_mode_)
    {
        CAFFE_ENFORCE_GE(T.ndim(), 2);
        CAFFE_ENFORCE_EQ(T.size_to_dim(canonical_axis), N);
        CAFFE_ENFORCE_EQ(T.size_from_dim(canonical_axis), D);
    }
    else
    {
        if(T.ndim() == canonical_axis)
        {
            CAFFE_ENFORCE_EQ(T.size(), N);
        }
        else
        {
            CAFFE_ENFORCE_EQ(T.size_to_dim(canonical_axis), N);
            CAFFE_ENFORCE_EQ(T.size_from_dim(canonical_axis), 1);
        }
    }

    avg_loss->Resize(vector<TIndex>());
    if(losses_.size() != N)
    {
        losses_.Resize(N);
    }
    if(rowmax_.size() != N)
    {
        rowmax_.Resize(N);
    }
    if(sum_multiplier_.size() != D)
    {
        sum_multiplier_.Resize(D);
        math::Set<float, HIPContext>(D, 1.f, sum_multiplier_.mutable_data<float>(), &context_);
    }
    Softmax(N,
            D,
            X.data<float>(),
            sum_multiplier_.data<float>(),
            losses_.mutable_data<float>(),
            rowmax_.mutable_data<float>(),
            P->mutable_data<float>(),
            !label_prob_mode_, // logarithmic output
            &context_);
    // Compute label xent loss per example
    if(!label_prob_mode_)
    {
        hipLaunchKernelGGL((LabelCrossEntropyKernel),
                           dim3(CAFFE_GET_BLOCKS(N)),
                           dim3(CAFFE_HIP_NUM_THREADS),
                           0,
                           context_.hip_stream(),
                           static_cast<const int>(N),
                           static_cast<const int>(D),
                           P->data<float>(),
                           T.data<int>(),
                           weights,
                           losses_.mutable_data<float>());
        // Since we had logarithmic output, we need to exponentiate
        // them again.
        math::Exp<float, HIPContext>(N * D, P->data<float>(), P->mutable_data<float>(), &context_);
    }
    else
    {
        hipLaunchKernelGGL((ProbCrossEntropyKernel),
                           dim3(std::min(N, CAFFE_MAXIMUM_NUM_BLOCKS)),
                           dim3(CAFFE_HIP_NUM_THREADS),
                           0,
                           context_.hip_stream(),
                           static_cast<const int>(N),
                           static_cast<const int>(D),
                           P->data<float>(),
                           T.data<float>(),
                           weights,
                           losses_.mutable_data<float>());
    }

    float total_weight = N;
    if(weights)
    {
        // Sum weights
        math::Sum<float, HIPContext>(
            N, weights, total_weight_ptr_.mutable_data<float>(), &context_, &scratch_);
        hipMemcpyAsync(&total_weight,
                       total_weight_ptr_.data<float>(),
                       sizeof(float),
                       hipMemcpyDeviceToHost,
                       context_.hip_stream());
    }

    // Sum of all losses
    float* avg_loss_data = avg_loss->mutable_data<float>();
    math::Sum<float, HIPContext>(
        losses_.size(), losses_.data<float>(), avg_loss_data, &context_, &scratch_);
    // Average of input batch size
    if(total_weight > 0)
    {
        math::Scale<float, HIPContext>(
            1, scale_ / total_weight, avg_loss_data, avg_loss_data, &context_);
    }

    return true;
}

template <>
bool SpatialSoftmaxWithLossOp<float, HIPContext>::RunOnDevice()
{
    auto& X              = Input(0);  // Logits
    auto& T              = Input(1);  // Labels / targets
    auto* P              = Output(0); // Probabilities from softmax
    auto* avg_loss       = Output(1); // Average loss
    const float* weights = (InputSize() > 2 ? Input(2).data<float>() : NULL);
    int N, D;
    N = X.dim32(0);
    D = X.dim32(1);
    P->ResizeLike(X);
    total_weight_ptr_.Resize(1);
    CAFFE_ENFORCE_EQ(X.ndim(), 4);
    CAFFE_ENFORCE_EQ(T.ndim(), 3);
    CAFFE_ENFORCE_EQ(T.dim32(0), N);

    int H = X.dim32(2);
    int W = X.dim32(3);
    if(losses_.size() != N * W * H)
    {
        losses_.Resize(N * W * H);
    }
    if(weights_.size() != N * W * H)
    {
        weights_.Resize(N * W * H);
    }

    const float* Xdata = X.data<float>();
    float* Pdata       = P->mutable_data<float>();

    // Softmax for each x,y location
    hipLaunchKernelGGL((SpatialSoftmaxKernel),
                       dim3(CAFFE_GET_BLOCKS(N)),
                       dim3(CAFFE_HIP_NUM_THREADS),
                       0,
                       context_.hip_stream(),
                       static_cast<const int>(N),
                       static_cast<const int>(D),
                       static_cast<const int>(W),
                       static_cast<const int>(H),
                       Xdata,
                       Pdata);

    // Cross entropy
    avg_loss->Resize(vector<TIndex>());
    float* avg_loss_data = avg_loss->mutable_data<float>();
    math::Set<float, HIPContext>(1, 0.0f, avg_loss_data, &context_);

    const int* label_data = T.data<int>();
    math::Set<float, HIPContext>(1, 0.0f, total_weight_ptr_.mutable_data<float>(), &context_);

    hipLaunchKernelGGL((SpatialCrossEntropyLossKernel),
                       dim3(CAFFE_GET_BLOCKS(N * W * H)),
                       dim3(CAFFE_HIP_NUM_THREADS),
                       0,
                       context_.hip_stream(),
                       static_cast<const int>(N),
                       static_cast<const int>(D),
                       static_cast<const int>(W),
                       static_cast<const int>(H),
                       P->data<float>(),
                       label_data,
                       weights,
                       losses_.mutable_data<float>(),
                       weights_.mutable_data<float>());

    // Somewhat awkward scalar passing from device to host
    float h_total_weight;
    math::Sum<float, HIPContext>(weights_.size(),
                                 weights_.data<float>(),
                                 total_weight_ptr_.mutable_data<float>(),
                                 &context_,
                                 &scratch_);
    hipMemcpyAsync(&h_total_weight,
                   total_weight_ptr_.data<float>(),
                   sizeof(float),
                   hipMemcpyDeviceToHost,
                   context_.hip_stream());

    math::Sum<float, HIPContext>(
        losses_.size(), losses_.data<float>(), avg_loss_data, &context_, &scratch_);

    // Final scaling
    if(h_total_weight > 0)
    {
        math::Scale<float, HIPContext>(
            1, scale_ / h_total_weight, avg_loss_data, avg_loss_data, &context_);
    }

    return true;
}

template <>
bool SoftmaxWithLossGradientOp<float, HIPContext>::RunOnDevice()
{
    auto& X = Input(0); // Logits
    auto& T = Input(1); // Labels / targets
    // Input(2) is weights, if given
    auto& P              = Input(InputSize() - 2); // Probabilities from softmax
    auto& d_avg_loss     = Input(InputSize() - 1); // Gradient w.r.t. avg loss
    const float* weights = (InputSize() > 4 ? Input(2).data<float>() : NULL);

    auto* dX = Output(0);
    dX->ResizeLike(X);

    const auto canonical_axis = X.canonical_axis_index(axis_);
    int N, D;
    N = X.size_to_dim(canonical_axis); // batch size
    D = X.size_from_dim(canonical_axis);

    if(only_loss_)
    {
        // Memory saving trick to share the buffer with the softmax output.
        // Softmax output is thus overwritten.
        dX->ShareData(P);
    }

    total_weight_ptr_.Resize(1);

    if(label_prob_mode_)
    {
        CAFFE_ENFORCE_GE(T.ndim(), 2);
        CAFFE_ENFORCE_EQ(T.size_to_dim(canonical_axis), N);
        CAFFE_ENFORCE_EQ(T.size_from_dim(canonical_axis), D);
    }
    else
    {
        if(T.ndim() == canonical_axis)
        {
            CAFFE_ENFORCE_EQ(T.size(), N);
        }
        else
        {
            CAFFE_ENFORCE_EQ(T.size_to_dim(canonical_axis), N);
            CAFFE_ENFORCE_EQ(T.size_from_dim(canonical_axis), 1);
        }
    }

    // Subtract 1 from labeled positions
    if(!label_prob_mode_)
    {
        if(weights == nullptr)
        {
            // Copy softmax probabilities into dX
            if(!only_loss_)
            {
                context_.Copy<float, HIPContext, HIPContext>(
                    P.size(), P.data<float>(), dX->mutable_data<float>());
            }
            hipLaunchKernelGGL((LabelCrossEntropyGradientKernel),
                               dim3(CAFFE_GET_BLOCKS(N)),
                               dim3(CAFFE_HIP_NUM_THREADS),
                               0,
                               context_.hip_stream(),
                               static_cast<const int>(N),
                               static_cast<const int>(D),
                               P.data<float>(),
                               T.data<int>(),
                               dX->mutable_data<float>());
        }
        else
        {
            // Weighted version gets the Pdata values internally
            hipLaunchKernelGGL((LabelCrossEntropyGradientKernelWeighted),
                               dim3(CAFFE_GET_BLOCKS(N * D)),
                               dim3(CAFFE_HIP_NUM_THREADS),
                               0,
                               context_.hip_stream(),
                               static_cast<const int>(N),
                               static_cast<const int>(D),
                               P.data<float>(),
                               T.data<int>(),
                               dX->mutable_data<float>(),
                               weights);
        }
    }
    else
    {
        hipLaunchKernelGGL((ProbCrossEntropyGradientKernel),
                           dim3(CAFFE_GET_BLOCKS(N * D)),
                           dim3(CAFFE_HIP_NUM_THREADS),
                           0,
                           context_.hip_stream(),
                           static_cast<const int>(N),
                           static_cast<const int>(D),
                           P.data<float>(),
                           T.data<float>(),
                           dX->mutable_data<float>(),
                           weights);
    }
    float total_weight = N;
    if(weights)
    {
        // Sum weights
        math::Sum<float, HIPContext>(
            N, weights, total_weight_ptr_.mutable_data<float>(), &context_, &scratch_);
        hipMemcpyAsync(&total_weight,
                       total_weight_ptr_.data<float>(),
                       sizeof(float),
                       hipMemcpyDeviceToHost,
                       context_.hip_stream());
    }

    // Scale by d_avg_loss / N
    if(total_weight > 0)
    {
        math::Scale<float, HIPContext>(dX->size(),
                                       scale_ / total_weight,
                                       dX->data<float>(),
                                       dX->mutable_data<float>(),
                                       &context_);
    }
    math::Scale<float, HIPContext>(dX->size(),
                                   d_avg_loss.data<float>(),
                                   dX->data<float>(),
                                   dX->mutable_data<float>(),
                                   &context_);

    return true;
}

template <>
bool SpatialSoftmaxWithLossGradientOp<float, HIPContext>::RunOnDevice()
{
    auto& X = Input(0); // Logits
    auto& T = Input(1); // Labels / targets
    // Input(2) is weights, if given
    auto& P              = Input(InputSize() - 2); // Probabilities from softmax
    auto& d_avg_loss     = Input(InputSize() - 1); // Gradient w.r.t. avg loss
    const float* weights = (InputSize() > 4 ? Input(2).data<float>() : NULL);

    auto* dX = Output(0);
    dX->ResizeLike(X);

    const auto canonical_axis = X.canonical_axis_index(1);
    int N, D;
    N = X.dim32(0);
    D = X.dim32(1);

    if(only_loss_)
    {
        // Memory saving trick to share the buffer with the softmax output.
        // Softmax output is thus overwritten.
        dX->ShareData(P);
    }

    total_weight_ptr_.Resize(1);
    // Spatial mode, compute softmax for each x, y location
    CAFFE_ENFORCE_EQ(X.ndim(), 4);
    CAFFE_ENFORCE_EQ(T.ndim(), 3);

    int H = X.dim32(2);
    int W = X.dim32(3);
    dX->ResizeLike(X);
    if(weights_.size() != N * W * H)
    {
        weights_.Resize(N * W * H);
    }

    const float* Pdata           = P.data<float>();
    float* dX_data               = dX->mutable_data<float>();
    const int* label_data        = T.data<int>();
    const float* d_avg_loss_data = d_avg_loss.data<float>();

    // Copy softmax probabilities into dX. All but the neuron
    // corresponding to the correct label has gradient equaling e(x_j)
    // which is the probability under softmax.
    context_.Copy<float, HIPContext, HIPContext>(P.size(), Pdata, dX_data);

    math::Set<float, HIPContext>(1, 0.0f, total_weight_ptr_.mutable_data<float>(), &context_);

    hipLaunchKernelGGL((SpatialSoftmaxLossGradientKernel),
                       dim3(CAFFE_GET_BLOCKS(N * W * H)),
                       dim3(CAFFE_HIP_NUM_THREADS),
                       0,
                       context_.hip_stream(),
                       static_cast<const int>(N),
                       static_cast<const int>(D),
                       static_cast<const int>(W),
                       static_cast<const int>(H),
                       label_data,
                       weights,
                       dX_data,
                       weights_.mutable_data<float>());

    math::Sum<float, HIPContext>(weights_.size(),
                                 weights_.data<float>(),
                                 total_weight_ptr_.mutable_data<float>(),
                                 &context_,
                                 &scratch_);

    // Somewhat awkward scalar passing from device to host
    float h_total_weight;
    hipMemcpyAsync(&h_total_weight,
                   total_weight_ptr_.data<float>(),
                   sizeof(float),
                   hipMemcpyDeviceToHost,
                   context_.hip_stream());

    // Final scaling
    if(h_total_weight > 0)
    {
        math::Scale<float, HIPContext>(dX->size(),
                                       scale_ / h_total_weight,
                                       dX->data<float>(),
                                       dX->mutable_data<float>(),
                                       &context_);
    }
    math::Scale<float, HIPContext>(dX->size(),
                                   d_avg_loss.data<float>(),
                                   dX->data<float>(),
                                   dX->mutable_data<float>(),
                                   &context_);

    return true;
}

// Implementation for the HIP context.
template <>
bool SoftmaxOp<float, HIPContext>::RunOnDevice()
{
    auto& X                   = Input(0);
    auto* P                   = Output(0);
    const auto canonical_axis = X.canonical_axis_index(axis_);
    const int N               = X.size_to_dim(canonical_axis);
    const int D               = X.size_from_dim(canonical_axis);
    P->ResizeLike(X);
    if(sum_multiplier_.size() != D)
    {
        sum_multiplier_.Resize(D);
        math::Set<float, HIPContext>(D, 1.f, sum_multiplier_.mutable_data<float>(), &context_);
    }
    if(scale_.size() != N)
    {
        scale_.Resize(N);
    }
    if(rowmax_.size() != N)
    {
        rowmax_.Resize(N);
    }
    Softmax(N,
            D,
            X.data<float>(),
            sum_multiplier_.data<float>(),
            scale_.mutable_data<float>(),
            rowmax_.mutable_data<float>(),
            P->mutable_data<float>(),
            false,
            &context_);
    return true;
}
#define SOFTMAX_NUM_THREADS 128

// The softmax gradient kernel. This kernel has to be called with the number of
// threads per block being no more than SOFTMAX_NUM_THREADS.
namespace {
__global__ void softmax_gradient_kernel(const int dim, const float* Y, const float* dY, float* dX)
{
    Y += hipBlockIdx_x * dim;
    dY += hipBlockIdx_x * dim;
    dX += hipBlockIdx_x * dim;
    const int idx = hipThreadIdx_x;
    __shared__ float reduction_buffer[SOFTMAX_NUM_THREADS];
    float tmp;

    // A two-level reduction to compute the inner products.
    tmp = 0;
    for(int i = idx; i < dim; i += hipBlockDim_x)
    {
        tmp += dY[i] * Y[i];
    }
    reduction_buffer[idx] = tmp;
    __syncthreads();
    if(idx == 0)
    {
        tmp = reduction_buffer[0];
        for(int i = 1; i < hipBlockDim_x; ++i)
            tmp += reduction_buffer[i];
        reduction_buffer[0] = tmp;
    }
    __syncthreads();
    // Compute gradient.
    tmp = reduction_buffer[0];
    for(int i = idx; i < dim; i += hipBlockDim_x)
    {
        dX[i] = Y[i] * (dY[i] - tmp);
    }
}
} // namespace

template <>
bool SoftmaxGradientOp<float, HIPContext>::RunOnDevice()
{
    auto& Y                   = Input(0);
    auto& dY                  = Input(1);
    auto* dX                  = Output(0);
    const auto canonical_axis = Y.canonical_axis_index(axis_);
    const int N               = Y.size_to_dim(canonical_axis);
    const int D               = Y.size_from_dim(canonical_axis);
    dX->ResizeLike(Y);
    hipLaunchKernelGGL((softmax_gradient_kernel),
                       dim3(N),
                       dim3(SOFTMAX_NUM_THREADS),
                       0,
                       context_.hip_stream(),
                       D,
                       Y.data<float>(),
                       dY.data<float>(),
                       dX->mutable_data<float>());
    return true;
}

REGISTER_HIP_OPERATOR(SoftmaxWithLoss, SoftmaxWithLossOp<float, HIPContext>);
REGISTER_HIP_OPERATOR(SoftmaxWithLossGradient, SoftmaxWithLossGradientOp<float, HIPContext>);
REGISTER_HIP_OPERATOR(SpatialSoftmaxWithLoss, SpatialSoftmaxWithLossOp<float, HIPContext>);
REGISTER_HIP_OPERATOR(SpatialSoftmaxWithLossGradient,
                      SpatialSoftmaxWithLossGradientOp<float, HIPContext>);
REGISTER_HIP_OPERATOR(Softmax, SoftmaxOp<float, HIPContext>);
REGISTER_HIP_OPERATOR(SoftmaxGradient, SoftmaxGradientOp<float, HIPContext>);

} // namespace caffe2
