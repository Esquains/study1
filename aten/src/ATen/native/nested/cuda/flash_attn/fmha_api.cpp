/******************************************************************************
 * Copyright (c) 2022, Tri Dao.
 * Copyright (c) 2011-2021, NVIDIA CORPORATION.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the NVIDIA CORPORATION nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL NVIDIA CORPORATION BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 ******************************************************************************/

// #include <torch/extension.h>


#include <cstddef>

#include <ATen/ATen.h>
#include <ATen/NativeFunctions.h>
#include <ATen/cuda/CUDAContext.h>
#include <ATen/native/nested/cuda/flash_attn/fmha_utils.h>
#include <ATen/native/nested/cuda/flash_attn/fmha.h>
#include <ATen/native/nested/cuda/flash_attn/fmha_api.h>

#include <c10/util/Exception.h>

#define CHECK_SHAPE(x, ...) TORCH_CHECK(x.sizes() == at::IntArrayRef({__VA_ARGS__}), #x " must have shape (" #__VA_ARGS__ ")")


namespace fmha{

void set_params_fprop(FMHA_fprop_params &params,
                      // sizes
                      const size_t b,
                      const size_t seqlen_q,
                      const size_t seqlen_k,
                      const size_t h,
                      const size_t d,
                      // device pointers
                      const at::Tensor q,
                      const at::Tensor k,
                      const at::Tensor v,
                      void *cu_seqlens_q_d,
                      void *cu_seqlens_k_d,
                      void *o_packed_d,
                      void *o_tmp_d,
                      void *s_d,
                      void *softmax_lse_d,
                      float softmax_scale,
                      bool is_causal) {

    // Data_type acc_type = DATA_TYPE_FP32;
    Data_type data_type = DATA_TYPE_FP16;

    // Reset the parameters
    memset(&params, 0, sizeof(params));

    // Set the pointers and strides.
    params.q_ptr = q.data_ptr();
    params.k_ptr = k.data_ptr();
    params.v_ptr = v.data_ptr();
    params.q_row_stride_in_elts = q.stride(0);
    params.k_row_stride_in_elts = k.stride(0);
    params.v_row_stride_in_elts = v.stride(0);
    params.q_head_stride_in_elts = q.stride(1);
    params.k_head_stride_in_elts = k.stride(1);
    params.v_head_stride_in_elts = v.stride(1);
    params.o_ptr = o_packed_d;
    params.o_row_stride_in_elts = h * d;
    params.o_head_stride_in_elts = d;
    params.o_tmp_ptr = o_tmp_d;

    params.cu_seqlens_q = static_cast<int *>(cu_seqlens_q_d);
    params.cu_seqlens_k = static_cast<int *>(cu_seqlens_k_d);

    // S = softmax(P)
    params.s_ptr = s_d;
    params.s_stride_in_bytes = get_size_in_bytes(b * h * seqlen_k, data_type);

    // Softmax sum
    params.softmax_lse_ptr = softmax_lse_d;

    // Set the dimensions.
    params.b = b;
    params.h = h;
    params.seqlen_q = seqlen_q;
    params.seqlen_k = seqlen_k;
    params.d = d;

    // Set the different scale values.
    // const float scale_bmm1 = 1.f / sqrtf(d);
    const float scale_bmm1 = softmax_scale;

    params.scale_bmm1f = scale_bmm1;
    set_alpha(params.scale_bmm1, scale_bmm1, data_type);

    params.is_causal = is_causal;
}

std::vector<at::Tensor>
mha_fwd(const at::Tensor &q,         // total_q x num_heads x head_size, total_q := \sum_{i=0}^{b} s_i
        const at::Tensor &k,         // total_k x num_heads x head_size, total_k := \sum_{i=0}^{b} s_i
        const at::Tensor &v,         // total_k x num_heads x head_size, total_k := \sum_{i=0}^{b} s_i
        const at::Tensor &cu_seqlens_q,  // b+1
        const at::Tensor &cu_seqlens_k,  // b+1
        const int max_seqlen_q_,
        const int max_seqlen_k_,
        const float softmax_scale,
        const bool zero_tensors,
        const bool is_causal,
        const bool return_softmax,
        c10::optional<at::Generator> gen_) {

    auto dprops = at::cuda::getCurrentDeviceProperties();
    bool is_sm75 = dprops->major == 7 && dprops->minor == 5;
    bool is_sm8x = dprops->major == 8 && dprops->minor >= 0;
    TORCH_CHECK(is_sm8x || is_sm75);
    auto stream = at::cuda::getCurrentCUDAStream().stream();
    Launch_params<FMHA_fprop_params> launch_params(dprops, stream, return_softmax);


    TORCH_CHECK(q.dtype() == at::kHalf);
    TORCH_CHECK(k.dtype() == at::kHalf);
    TORCH_CHECK(v.dtype() == at::kHalf);

    // TORCH_CHECK(q.dtype() == torch::kFloat16);
    // TORCH_CHECK(k.dtype() == torch::kFloat16);
    // TORCH_CHECK(v.dtype() == torch::kFloat16);
    TORCH_CHECK(cu_seqlens_q.dtype() == at::kInt);
    TORCH_CHECK(cu_seqlens_k.dtype() == at::kInt);

    TORCH_CHECK(q.is_cuda());
    TORCH_CHECK(k.is_cuda());
    TORCH_CHECK(v.is_cuda());
    TORCH_CHECK(cu_seqlens_q.is_cuda());
    TORCH_CHECK(cu_seqlens_k.is_cuda());

    TORCH_CHECK(q.stride(-1) == 1);
    TORCH_CHECK(k.stride(-1) == 1);
    TORCH_CHECK(v.stride(-1) == 1);
    TORCH_CHECK(cu_seqlens_k.is_contiguous());
    TORCH_CHECK(cu_seqlens_k.is_contiguous());

    const auto sizes = q.sizes();

    const int batch_size = cu_seqlens_q.numel() - 1;
    const int total_q = sizes[TOTAL_DIM];
    const int num_heads = sizes[H_DIM];
    const int head_size = sizes[D_DIM];
    const int total_k = k.size(TOTAL_DIM);
    TORCH_CHECK(batch_size > 0);
    TORCH_CHECK(head_size == 16 || head_size == 32 || head_size == 64 || head_size == 128);

    CHECK_SHAPE(q, total_q, num_heads, head_size);
    CHECK_SHAPE(k, total_k, num_heads, head_size);
    CHECK_SHAPE(v, total_k, num_heads, head_size);
    CHECK_SHAPE(cu_seqlens_q, batch_size + 1);
    CHECK_SHAPE(cu_seqlens_k, batch_size + 1);

    int blocksize_c = 256;
    // Need to round max_seqlen_k to multiples of blocksize_c
    int max_seqlen_k = ((max_seqlen_k_ + blocksize_c - 1) / blocksize_c) * blocksize_c;
    if( max_seqlen_k_ <= 128 ) {
        max_seqlen_k = 128;
    } else if( max_seqlen_k_ <= 256 ) {
        max_seqlen_k = 256;
    }
    int max_seqlen_q = ((max_seqlen_q_ + 16 - 1) / 16) * 16;
    bool loop = max_seqlen_k > blocksize_c;

    auto opts = q.options();

    auto o = at::empty({ total_q, num_heads, head_size }, opts);

    at::Tensor o_tmp;
    if (loop) { o_tmp = at::empty({total_q, num_heads, head_size}, opts.dtype(at::kFloat)); }

    auto softmax_lse = at::empty({batch_size, num_heads, max_seqlen_q}, opts.dtype(at::kFloat));
    // auto softmax_lse = torch::full({batch_size, num_heads, max_seqlen_k}, -std::numeric_limits<float>::infinity(), opts.dtype(at::kFloat));

    at::Tensor s;
    if (return_softmax) { s = at::empty({ batch_size, num_heads, max_seqlen_q, max_seqlen_k }, opts); }

    if( zero_tensors ) {
        o.zero_();
        softmax_lse.fill_(-std::numeric_limits<float>::infinity());
        if (return_softmax) {s.zero_();}
    }

    set_params_fprop(launch_params.params,
                     batch_size,
                     max_seqlen_q,
                     max_seqlen_k,
                     num_heads,
                     head_size,
                     q, k, v,
                     cu_seqlens_q.data_ptr(),
                     cu_seqlens_k.data_ptr(),
                     o.data_ptr(),
                     loop ? o_tmp.data_ptr() : nullptr,
                     return_softmax ? s.data_ptr() : nullptr,
                     softmax_lse.data_ptr(),
                     softmax_scale,
                     is_causal);

    run_fmha_fp16_sm80(launch_params, /*configure=*/ true);


    run_fmha_fp16_sm80(launch_params, /*configure=*/false);
    // return o;
    std::vector<at::Tensor> result = {o, softmax_lse};
    if (return_softmax) {result.push_back(s);}
    return result;
}

} // namespace fmha
