#include <ATen/ATen.h>
#include <ATen/Context.h>
#include <ATen/Dispatch.h>
#include <ATen/cuda/CUDAContext.h>
#include <ATen/cuda/CUDAEvent.h>
#include <c10/cuda/CUDAStream.h>
#include <ATen/native/Copy.h>
#include <ATen/native/TensorIterator.h>
#include <ATen/native/cuda/Loops.cuh>
#include <THC/THC.h>

namespace at {
namespace native {

using namespace at::cuda;

template <typename dst_t, typename src_t>
void copy_kernel_impl(TensorIterator& iter) {
  gpu_unary_kernel(iter, []GPU_LAMBDA(src_t x) -> dst_t {
    return static_cast<dst_t>(static_cast<native::inter_copy_type_t<dst_t>>(x));
  });
}

// device-to-device copy, does type conversion
static void copy_device_to_device(TensorIterator& iter, bool non_blocking) {
  static int64_t c1 = 0;
  c1 ++;
  if (c1 % 100 == 0) {
    std::cout << "GPU copy_device_to_device " << c1 << "\n";
  }
  int64_t numel = iter.numel();

  // We can memcpy the memory if both tensors have the same type AND both
  // tensors are contiguous after dimension coalescing and reordering.
  bool same_type = iter.dtype(0) == iter.dtype(1);
  bool cl_cont = iter.is_channels_last_contiguous();
  bool memcpy_eligible = same_type && ( iter.is_contiguous() || cl_cont );
  if (same_type && iter.is_contiguous()) {
    static int64_t c1 = 0;
    c1 ++;
    if (c1 % 100 == 0) {
      std::cout << "GPU copy_device_to_device is_contiguous == true " << c1 << "\n";
    }
  }

  if (same_type && cl_cont) {
    static int64_t c1 = 0;
    c1 ++;
    if (c1 % 100 == 0) {
      std::cout << "GPU copy_device_to_device is_channels_last_contiguous == true " << c1 << "\n";
    }
  }

  if (same_type && iter.is_channels_last_contiguous_all()) {
    static int64_t c11 = 0;
    c11 ++;
    if (c11 % 100 == 0) {
      std::cout << "GPU copy_device_to_device BOTH ChannelsLast == true " << c11 << "\n";
    }
  }

  Device dst_device = iter.device(0);
  Device src_device = iter.device(1);

  CUDAGuard device_guard(src_device);

  // We always perform the copy on the source device, using the current stream
  // on the source device, and we fully synchronize on both src and dst's
  // current streams for completion of the copy. We have to explicitly do this
  // for non-contig copies. This mimics the behavior of cross-device
  // cudaMemcpyAsync on the default stream.
  CUDAStream copy_stream = getCurrentCUDAStream(src_device.index());
  if (src_device != dst_device) {
    // This is a cross-device copy on the src current stream and dst current
    // stream. We perform a two-way barrier between both devices' streams
    // before the copy. This ensures that any write-after-write and
    // write-after-read dependencies on the destination side are handled, so
    // that no one is operating on the dst memory when we perform the copy.
    // src waits on dst barrier (src already waits on src)
    CUDAEvent dst_ready;
    device_guard.set_device(dst_device);
    dst_ready.record(getCurrentCUDAStream(dst_device.index()));

    device_guard.set_device(src_device);
    dst_ready.block(copy_stream);
  }

  if (memcpy_eligible) {
    static int64_t c2 = 0;
    c2 ++;
    if (c2  % 100 == 0) {
      std::cout << "GPU same memcopy " << c2 << "\n";
    }
    // Perform the copy
    AT_CUDA_CHECK(cudaMemcpyAsync(
        iter.data_ptr(0),
        iter.data_ptr(1),
        numel * iter.element_size(0),
        cudaMemcpyDeviceToDevice,
        copy_stream));
  } else {
    static int64_t c3 = 0;
    c3++;
    if (c3 % 100 == 0) {
      std::cout << "GPU iterator copy " << c3 << "\n";
    }
    AT_DISPATCH_ALL_TYPES_AND2(kHalf, kBool, iter.dtype(0), "copy_", [&] {
      using dst_t = scalar_t;
      AT_DISPATCH_ALL_TYPES_AND2(kHalf, kBool, iter.dtype(1), "copy_", [&] {
        copy_kernel_impl<dst_t, scalar_t>(iter);
      });
    });
  }

  if (src_device != dst_device) {
    // dst waits on src barrier (dst already waits on dst). We cannot
    // operate on dst's copy until the copy is complete.

    // Still on src_device, record stream event
    CUDAEvent src_ready;
    src_ready.record(copy_stream);

    device_guard.set_device(dst_device);
    src_ready.block(getCurrentCUDAStream(dst_device.index()));
  }

  AT_CUDA_CHECK(cudaGetLastError());
}

static bool copy_requires_temporaries(TensorIterator& iter) {
  Device dst_device = iter.device(0);
  Device src_device = iter.device(1);

  if (dst_device == src_device) {
    // We never require temporaries for copies on the same GPU.
    TORCH_INTERNAL_ASSERT(dst_device.is_cuda() && src_device.is_cuda());
    return false;
  }

  bool same_dtype = iter.dtype(0) == iter.dtype(1);
  if (same_dtype && iter.is_contiguous()) {
    // Contiguous same-dtype copies can always use cudaMemcpyAsync
    return false;
  } else if (dst_device.is_cuda() && src_device.is_cuda()) {
    // Copies between GPUs can use the copy kernel if P2P is supported
    return !THCState_getPeerToPeerAccess(
        globalContext().getTHCState(), src_device.index(), dst_device.index());
  } else {
    // The remaining cases require temporaries. For example, this includes
    // non-contiguous copies between CPU and GPU.
    return true;
  }
}

static void copy_kernel_cuda(TensorIterator& iter, bool non_blocking) {
  AT_ASSERT(iter.ntensors() == 2);

  if (copy_requires_temporaries(iter)) {
    // NB: this involves recursive calls to copy. Be careful that those copies
    // don't require temporaries or you will cause an infinite recursion!
    auto& dst = iter.tensor(0);
    Tensor dst_contig;
    Tensor src_contig;

    // Type conversions are performed on the CPU for CPU-GPU copies and on
    // the src device for GPU-GPU copies.
    if (iter.device_type(0) == kCUDA) {
      dst_contig = dst.is_contiguous() ? dst : at::empty_like(dst);
      src_contig = iter.tensor(1).to(iter.dtype(0)).expand_as(dst).contiguous();
    } else {
      bool same_type = iter.dtype(0) == iter.dtype(1);
      dst_contig = (dst.is_contiguous() && same_type) ? dst : at::empty_like(dst, iter.dtype(1));
      src_contig = iter.tensor(1).expand_as(dst).contiguous();
    }

    // perform a same-dtype copy on contiguous tensors
    TORCH_INTERNAL_ASSERT(dst_contig.sizes().equals(src_contig.sizes()));
    TORCH_INTERNAL_ASSERT(dst_contig.scalar_type() == src_contig.scalar_type());
    dst_contig.copy_(src_contig, non_blocking);

    // if necessary, copy back into dst
    if (!dst_contig.is_same(dst)) {
      TORCH_INTERNAL_ASSERT(dst_contig.device() == dst.device());
      dst.copy_(dst_contig, non_blocking);
    }
    return;
  }

  Device dst_device = iter.device(0);
  Device src_device = iter.device(1);

  // Copy on GPU (or between GPUs)
  if (dst_device.is_cuda() && src_device.is_cuda()) {
    copy_device_to_device(iter, non_blocking);
    return;
  }

  // Copy between CPU and GPU

  static int64_t c4 = 0;
  c4++;
  if (c4  % 100 == 0) {
    std::cout << "copy CPU <-> GPU " << c4 << "\n";
  }

  cuda::OptionalCUDAGuard device_guard;
  cudaMemcpyKind kind;
  if (dst_device.is_cuda() && src_device.is_cpu()) {
    device_guard.set_device(dst_device);
    kind = cudaMemcpyHostToDevice;
  } else if (dst_device.is_cpu() && src_device.is_cuda()) {
    device_guard.set_device(src_device);
    kind = cudaMemcpyDeviceToHost;
  } else {
    TORCH_INTERNAL_ASSERT(false, "unsupported devices in GPU copy_()");
  }

  void* dst = iter.data_ptr(0);
  void* src = iter.data_ptr(1);
  int64_t nbytes = iter.numel() * iter.element_size(0);
  CUDAStream stream = getCurrentCUDAStream();

  AT_CUDA_CHECK(cudaMemcpyAsync(dst, src, nbytes, kind, stream));

  if (non_blocking) {
    void* ptr = (dst_device == kCPU ? dst : src);
    AT_CUDA_CHECK(THCCachingHostAllocator_recordEvent(ptr, stream));
  } else {
    AT_CUDA_CHECK(cudaStreamSynchronize(stream));
  }
}

REGISTER_DISPATCH(copy_stub, &copy_kernel_cuda);

} // namespace native
} // namespace at
