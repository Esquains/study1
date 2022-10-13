// This file is auto-generated. See "generate_kernels.sh"
#include <ATen/native/transformers/cuda/mem_eff_attention/kernel_forward.h>
INSTANTIATE_ATTENTION_KERNEL_FORWARD_SM50(
    cutlass::half_t,
    false,
    32,
    128,
    true);
INSTANTIATE_ATTENTION_KERNEL_FORWARD_SM50(
    cutlass::half_t,
    false,
    32,
    128,
    false);
INSTANTIATE_ATTENTION_KERNEL_FORWARD_SM50(cutlass::half_t, false, 64, 64, true);
INSTANTIATE_ATTENTION_KERNEL_FORWARD_SM70(
    cutlass::half_t,
    false,
    32,
    128,
    true);
INSTANTIATE_ATTENTION_KERNEL_FORWARD_SM70(
    cutlass::half_t,
    false,
    32,
    128,
    false);
INSTANTIATE_ATTENTION_KERNEL_FORWARD_SM70(cutlass::half_t, false, 64, 64, true);
INSTANTIATE_ATTENTION_KERNEL_FORWARD_SM75(
    cutlass::half_t,
    false,
    32,
    128,
    true);
INSTANTIATE_ATTENTION_KERNEL_FORWARD_SM75(
    cutlass::half_t,
    false,
    32,
    128,
    false);
INSTANTIATE_ATTENTION_KERNEL_FORWARD_SM75(cutlass::half_t, false, 64, 64, true);
INSTANTIATE_ATTENTION_KERNEL_FORWARD_SM80(
    cutlass::half_t,
    false,
    32,
    128,
    true);
INSTANTIATE_ATTENTION_KERNEL_FORWARD_SM80(
    cutlass::half_t,
    false,
    32,
    128,
    false);
INSTANTIATE_ATTENTION_KERNEL_FORWARD_SM80(cutlass::half_t, false, 64, 64, true);
