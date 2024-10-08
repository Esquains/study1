#include <ATen/Functions.h>
#include <ATen/cuda/CUDAGeneratorImpl.h>
#include <ATen/cuda/CUDAGraph.h>
#include <ATen/cuda/Exceptions.h>
#include <ATen/cuda/nvrtc_stub/ATenNVRTC.h>
#include <c10/core/CPUAllocator.h>
#include <c10/cuda/CUDACachingAllocator.h>
#include <c10/cuda/CUDAFunctions.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <thread>
#include <vector>
#include <cuda_runtime.h>

namespace at::cuda {

static bool _cuda_graphs_debug = false;
constexpr int kSynchronizeBusyWaitMillis = 10;

MempoolId_t graph_pool_handle() {
  // Sets just the second value, to distinguish it from MempoolId_ts created from
  // cudaStreamGetCaptureInfo id_s in capture_begin.
  auto new_pool = c10::cuda::MemPool();
  return new_pool.id();
}

/**
 * Note [CUDA Graph Wrapper Class]
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 * Q: Why do we need graph capture and launch bindings in Pytorch?
 *    Why can't they live in a user extension, for example?
 *
 * A1: Convenience.
 * A2: To ensure valid numerics on replay, some native CUDA ops (like RNG ops with
 *     CPU statefulness) need cooperation from the capture and replay bindings
 *     (see Note [CUDA Graph-safe RNG states] in CUDAGeneratorImpl.h).
 *
 *     We can't expect users to know about this cooperation.  If users write capture
 *     bindings naively in an extension, they likely won't interact with the native
 *     ops properly.  Their graphs would yield invalid numerics on replay.
 */

/**
 * Note [Interaction with CUDA graph capture] in CUDACachingAllocator.cpp
 * describes memory management for captures.
 */

std::atomic<int> CUDAGraph::pending_event_queries = 0;

// Track any outstanding event queries that could happen e.g., in a NCCL watchdog so that they
// can be resolved before the capture begins. Note that event queries are not allowed during a
// graph capture in the default capture mode.
void CUDAGraph::inc_pending_event_queries() {
  pending_event_queries++;
}

void CUDAGraph::dec_pending_event_queries() {
  TORCH_INTERNAL_ASSERT(pending_event_queries > 0,
    "Attempted to decrement the number of outstanding events to be queried, but it was <= 0.");
  pending_event_queries--;
}

int CUDAGraph::num_pending_event_queries() {
  return pending_event_queries;
}

CUDAGraph::CUDAGraph()
  // CUDAStreams may not be default-constructed.
  : capture_stream_(at::cuda::getCurrentCUDAStream()) {
}

void CUDAGraph::register_generator_state(
    c10::intrusive_ptr<at::CUDAGeneratorState> state) {
  captured_generator_states_[std::move(state)] = 0;
}

void CUDAGraph::register_generator_state(const at::Generator& generator) {
  c10::intrusive_ptr<CUDAGeneratorImpl> cuda_gen =
      dynamic_intrusive_pointer_cast<CUDAGeneratorImpl>(
          generator.getIntrusivePtr());
  cuda_gen->register_graph(this);
}

std::atomic<size_t> captureUniqueToken{1};

constexpr int kAllocationStride = 1024; // a misaligned data pointer will freak out the kernels
constexpr int kAllocationBase = 2048; // nullptr freaks out various APIs throughout torch

void CUDAGraph::capture_begin(MempoolId_t pool/*=0*/, cudaStreamCaptureMode capture_mode, int sentinel_allocations_mode) {
  TORCH_CHECK(!has_graph_exec_,
              "This CUDAGraph instance already owns a captured graph. "
              "To capture a new graph, create a new instance.");

  // default generator is always registered
  auto* gen = get_generator_or_default<CUDAGeneratorImpl>(
      std::nullopt, cuda::detail::getDefaultCUDAGenerator());
  gen->register_graph(this);

  for (auto& [generator_state, wholegraph_increments] :
       captured_generator_states_) {
    generator_state->capture_prologue();
  }

  auto stream = at::cuda::getCurrentCUDAStream();

  TORCH_CHECK(stream != at::cuda::getDefaultCUDAStream(),
              "CUDA graphs must be captured on a non-default stream. "
              "(However, after capture, it's ok to replay them on the "
              "default stream.)");

  capture_stream_ = stream;
  capture_dev_ = c10::cuda::current_device();

  if (pool.first != 0 || pool.second != 0) {
    // Either value being nonzero means the user supplied a pool to share.
    // But only one should be nonzero.
    // If pool was created by another graph's capture_begin, first should be nonzero.
    // If pool was created by graph_pool_handle, second should be nonzero.
    TORCH_INTERNAL_ASSERT(!(pool.first && pool.second));
    mempool_id_ = pool;
  } else {
    // User did not ask us to share a mempool. Create graph pool handle using is_user_created=false.
    // Sets just the first value, to distinguish it from MempoolId_ts created by graph_pool_handle().
    auto mempool = c10::cuda::MemPool({}, false);
    mempool_id_ = mempool.id();
    TORCH_INTERNAL_ASSERT(mempool_id_.first > 0);
  }

  // Addendum: beginAllocateStreamToPool is now called before cudaStreamBeginCapture to prevent an
  // autograd thread's free() call triggering an invalid cudaEventRecord in the caching allocator
  // due to the capture status being updated _after_ a capture had already started.
  c10::cuda::CUDACachingAllocator::beginAllocateToPool(capture_dev_, mempool_id_, [this](cudaStream_t stream) {
      cudaStreamCaptureStatus status;
      CaptureId_t stream_capture_id;
      AT_CUDA_CHECK(cudaStreamGetCaptureInfo(stream, &status, &stream_capture_id));
      return status == cudaStreamCaptureStatus::cudaStreamCaptureStatusActive && stream_capture_id == capture_id_;
  });

  sentinelAllocationsMode = sentinel_allocations_mode;

  if (sentinelAllocationsMode) {
    sentinelCaptureUniqueToken = captureUniqueToken++;
    sentinelAllocationIdx = 0;

    // captures_underway is pretty complicated - no reason to duplicate that effort. let's call it regardless
    // just let's also intercept the malloc calls
    c10::cuda::CUDACachingAllocator::beginAllocateSentinelPointers(capture_dev_, [this](cudaStream_t stream) {
        cudaStreamCaptureStatus status;
        CaptureId_t stream_capture_id;
        AT_CUDA_CHECK(cudaStreamGetCaptureInfo(stream, &status, &stream_capture_id));
        return status == cudaStreamCaptureStatus::cudaStreamCaptureStatusActive && stream_capture_id == capture_id_;
    }, [this](size_t allocSz) {
      allocationSizes.push_back(allocSz);
      if (sentinelAllocationsMode == 1) {
        std::cout << "intercepted alloc of size " << allocSz << " returning " << kAllocationBase << " since mode=1" << std::endl;
        return (void*)((char*)nullptr + kAllocationBase);
      } else {
        std::cout << "intercepted alloc of size " << allocSz << " returning allocIdx+1 since mode=2" << std::endl;
        // I think that alignment is causing issues... The kernels can
        // inspect the alignment to decide what amount of
        // vectorization is possible in the loads and stores, etc. We
        // want to make sure that the alignment is always "large
        // enough". Use 1024 for now...
        return (void*)((char*)nullptr + kAllocationBase + kAllocationStride * (1 + sentinelAllocationIdx++));
      }
    }, sentinelCaptureUniqueToken);
  }

  // At this point, any NCCL watchdogs should be aware that we are in capture mode
  // and therefore should not enqueue any additional work that could be event-queried.
  // We still must wait on any existing work that has not been cleaned up.
  while (num_pending_event_queries()) {
    TORCH_WARN_ONCE("Waiting for pending NCCL work to finish before starting graph capture.");
    std::this_thread::sleep_for(
      std::chrono::milliseconds(kSynchronizeBusyWaitMillis));
  }

  // cudaStreamCaptureModeGlobal is the most conservative option to
  // prevent potentially unsafe CUDA API calls during capture.  See
  // https://docs.nvidia.com/cuda/cuda-runtime-api/group__CUDART__STREAM.html#group__CUDART__STREAM_1g9d0535d93a214cbf126835257b16ba85
  AT_CUDA_CHECK(cudaStreamBeginCapture(capture_stream_, capture_mode));

  cudaStreamCaptureStatus status;
  AT_CUDA_CHECK(cudaStreamGetCaptureInfo(stream, &status, &capture_id_));
  TORCH_INTERNAL_ASSERT(status == cudaStreamCaptureStatus::cudaStreamCaptureStatusActive);

}

void CUDAGraph::capture_end() {
  auto stream = at::cuda::getCurrentCUDAStream();

  TORCH_CHECK(stream == capture_stream_,
              "Capture must end on the same stream it began on.");

  AT_CUDA_CHECK(cudaStreamEndCapture(capture_stream_, &graph_));

  c10::cuda::CUDACachingAllocator::endAllocateToPool(capture_dev_, mempool_id_);

  if (sentinelAllocationsMode) {
    c10::cuda::CUDACachingAllocator::endAllocateSentinelPointers(sentinelCaptureUniqueToken);
    std::cout << "capture_end, recorded allocation sizes were: " << allocationSizes << std::endl;
  }

  TORCH_CHECK(graph_ != nullptr, "Invalid capture.");
  has_graph_ = true;
  if (!sentinelAllocationsMode) {
    instantiate_graph_exec();
  }
}

void CUDAGraph::instantiate_graph_exec() {
  TORCH_CHECK(has_graph_, "must have graph");
  TORCH_CHECK(!has_graph_exec_, "must not have graph exec");
  // In typical graph usage some tensors (e.g. the tensors used for graph IO) are not freed
  // between replays.
  // If Pytorch compiles and runs with a CUDA 11.4+ toolkit, there's a chance the allocator backend
  // is cudaMallocAsync.
  // cudaMallocAsync is generally graph-safe, but if some tensors are not freed between replays,
  // the graph's internal bookkeeping requires that we instantiate with
  // cudaGraphInstantiateFlagAutoFreeOnLaunch. See
  // cudaGraphLaunch
  // https://docs.nvidia.com/cuda/cuda-runtime-api/group__CUDART__GRAPH.html#group__CUDART__GRAPH_1g1accfe1da0c605a577c22d9751a09597
  // cudaGraphInstantiateWithFlags
  // https://docs.nvidia.com/cuda/cuda-runtime-api/group__CUDART__GRAPH.html#group__CUDART__GRAPH_1ga2c652a24ba93e52b99a47bec0888233
#if (defined(CUDA_VERSION) && CUDA_VERSION >= 11040)
  int version = 0;
  AT_CUDA_CHECK(cudaDriverGetVersion(&version));
  if (version < 11040) {
#endif
    // Trailing NULL, NULL, 0 arguments were recommended by Cuda driver people,
    // who prefer not to report error message through these arguments moving forward
    // (they prefer return value, or errors on api calls internal to the capture)
#if (defined(CUDA_VERSION) && CUDA_VERSION >= 12000)
    AT_CUDA_CHECK(cudaGraphInstantiate(&graph_exec_, graph_, 0));
#else
    AT_CUDA_CHECK(cudaGraphInstantiate(&graph_exec_, graph_, NULL, NULL, 0));
#endif
#if (defined(CUDA_VERSION) && CUDA_VERSION >= 11040)
  } else {
    AT_CUDA_CHECK(cudaGraphInstantiateWithFlags(&graph_exec_,
                                                graph_,
                                                cudaGraphInstantiateFlagAutoFreeOnLaunch));
  }
#endif

  has_graph_exec_ = true;

  for (auto& [generator_state, wholegraph_increments] :
       captured_generator_states_) {
    wholegraph_increments = generator_state->capture_epilogue();
  }

  size_t numCUDAGraphNodes = 0;
  AT_CUDA_CHECK(cudaGraphGetNodes(graph_, nullptr, &numCUDAGraphNodes));
  if (numCUDAGraphNodes == 0) {
      TORCH_WARN("The CUDA Graph is empty. This usually means that the graph was ",
                 "attempted to be captured on wrong device or stream.");
  }

  // check if debug path is set
  if (!_cuda_graphs_debug) {
    // Now that we've instantiated graph_ into graph_exec_,
    // we don't need graph_ anymore.
    AT_CUDA_CHECK(cudaGraphDestroy(graph_));
    has_graph_ = false;
  } else {
    TORCH_WARN("DEBUG: TORCH_CUDAGRAPHS_DEBUG_PATH detected. graph_ will not be freed until debug_dump is called.");
  }
}

void CUDAGraph::replay() {
  TORCH_CHECK(has_graph_exec_,
              "Called CUDAGraph::replay without a preceding successful capture.");

  c10::OptionalDeviceGuard device_guard{capture_stream_.device()};

  for (auto& [generator_state, wholegraph_increments] :
       captured_generator_states_) {
    generator_state->replay_prologue(wholegraph_increments);
  }
  // graph_exec_ may be replayed in any stream.
  AT_CUDA_CHECK(cudaGraphLaunch(graph_exec_, at::cuda::getCurrentCUDAStream()));

  int version = 0;
  AT_CUDA_CHECK(cudaDriverGetVersion(&version));
  if (version < 11040) {
    // Workaround for bug in libcuda.so that causes replayed graphs with
    // certain topologies to be corrupted (kernels elided, internal syncs
    // ignored) when replayed back to back without a sync in between.
    // The bug is fixed in CUDA 11.4+.
    AT_CUDA_CHECK(cudaDeviceSynchronize());
  }
}

void CUDAGraph::enable_debug_mode() {
  _cuda_graphs_debug = true;
}

void CUDAGraph::debug_dump(const std::string& debug_path) {
#if (defined(CUDA_VERSION) && CUDA_VERSION >= 11030)|| defined(USE_ROCM)
  if (_cuda_graphs_debug) {
    TORCH_WARN("DEBUG: calling debug_dump()");
    if (has_graph_) {
      TORCH_WARN("DEBUG: calling cudaGraphDebugDotPrint() with ", debug_path);
      C10_CUDA_CHECK_WARN(cudaGraphDebugDotPrint(graph_, debug_path.c_str(), cudaGraphDebugDotFlagsVerbose)); // most verbose output
      AT_CUDA_CHECK(cudaGraphDestroy(graph_));
      has_graph_ = false;
    }
  } else {
    TORCH_WARN("CUDA Graphs debug not enabled, set with torch._C._cuda_enable_graphs_debug_mode");
  }
#else
  TORCH_CHECK(false, "CUDA graphs may only be used in Pytorch built with CUDA >= 11.3 or ROCM >= 5.6");
#endif
}

void CUDAGraph::reset() {
  // I'd prefer these checks throw exceptions, not print warnings,
  // but the destructor calls reset(), and at least one CI build
  // refuses to compile with a throwing destructor.
  //
  // Instead of calling reset() in the destructor to clean up, I could
  // call reset() in the __del__ method of a thin Python wrapper,
  // in which case reset would be allowed to throw exceptions.
  // But Stackoverflow does not like user-defined __del__.
  // __del__ prevents Graph instances from EVER being garbage collected
  // if they participate in a reference cycle.
  // And exceptions thrown in __del__ only print a warning anyway.
  //
  // Calling reset() in the C++ destructor, with warnings instead of exceptions
  // if calls fail, is the compromise we chose.
  //
  // If capture_begin, the capture, or capture_end failed at some point, this CUDAGraph, the generator,
  // and the allocator could end up in all kinds of weird states depending where failure occurred.
  // If the user catches the failure exception in a script, or is running in REPL or (god forbid)
  // a Jupyter notebook, I don't see an easy way for reset() to gracefully fix all such possible error states.
  if (has_graph_ || has_graph_exec_) {
    // notifyCaptureDestroy may throw. How should we handle this?
    c10::cuda::CUDACachingAllocator::releasePool(capture_dev_, mempool_id_);
  }
  if (has_graph_) {
    C10_CUDA_CHECK_WARN(cudaGraphDestroy(graph_));
    has_graph_ = false;
  }
  if (has_graph_exec_) {
    C10_CUDA_CHECK_WARN(cudaGraphExecDestroy(graph_exec_));
    has_graph_exec_ = false;
  }
}

// Returns an id another graph's capture_begin can use to share the same memory pool as this graph.
MempoolId_t CUDAGraph::pool() {
TORCH_CHECK(has_graph_exec_,
              "Called CUDAGraph::pool() without a preceding successful capture.");
  return mempool_id_;
}

CUDAGraph::~CUDAGraph() {
  for (auto& [generator_state, wholegraph_increments] :
       captured_generator_states_) {
    generator_state->unregister_graph(this);
  }
  reset();
}

std::vector<std::vector<UpdateAndTensorOffset>> create_device_updates(
    CUDAGraph* graph1_ptr,
    const CUDAGraph& graph2,
    const std::vector<std::pair<at::Tensor, at::Tensor>>& input_pairs) {
  return {};
}

void CUDAGraph::compare_with_recapture(const CUDAGraph& graph2) {
  // Note: graph1 and graph2 should topologically be the same
  // they should have come from doing stream capture twice, on two separate
  // inputs TORCH_CHECK(graph1.descendent_graphs_.empty());
  // TORCH_CHECK(graph2.descendent_graphs_.empty());

  TORCH_CHECK(sentinelAllocationsMode == 1, "This graph must have been captured in sentinel mode 1");
  TORCH_CHECK(graph2.sentinelAllocationsMode == 2, "The other graph must have been captured in sentinel mode 2");
  TORCH_CHECK(allocationSizes == graph2.allocationSizes, "Both graphs must have done the exact same allocations in the exact same order with the exact same sizes");
  TORCH_CHECK(has_graph_, "must have graph");
  TORCH_CHECK(!has_graph_exec_, "must not have graph exec");

  CUDAGraph& graph1 = *this;

  std::cout << "graph ptrs " << (void*)graph1.graph_ << " " << (void*)graph2.graph_ << std::endl;
  size_t num_nodes1;
  AT_CUDA_CHECK(cudaGraphGetNodes(graph1.graph_, nullptr, &num_nodes1));
  std::cout << "number of nodes captured " << num_nodes1 << std::endl;
  std::vector<cudaGraphNode_t> nodes1(num_nodes1);
  AT_CUDA_CHECK(cudaGraphGetNodes(graph1.graph_, nodes1.data(), &num_nodes1));

  size_t num_nodes2;
  AT_CUDA_CHECK(cudaGraphGetNodes(graph2.graph_, nullptr, &num_nodes2));
  std::vector<cudaGraphNode_t> nodes2(num_nodes2);
  AT_CUDA_CHECK(cudaGraphGetNodes(graph2.graph_, nodes2.data(), &num_nodes2));

  TORCH_CHECK_EQ(num_nodes1, num_nodes2);

  auto checkAllocation = [this](void* ptr1, void* ptr2) -> std::optional<std::tuple<size_t, size_t>> {
    // reverse engineer the sentinel pointers to determine which allocation this is (allocIdx) and the offset into the tensor (offset)
    int64_t whichAlloc = (int64_t)((char*)ptr2 - (char*)ptr1);
    if (whichAlloc % kAllocationStride != 0) {
      std::cout << "LEIJURV: MISMATCH the allocation strided by " << whichAlloc
                << " which is not divisible by our stride " << kAllocationStride 
                << " so this is not one of ours :(" << std::endl;
      return std::nullopt;
    }
    
    int64_t allocIdx = whichAlloc / kAllocationStride - 1;
    if (allocIdx < 0 || allocIdx >= (int64_t)allocationSizes.size()) {
      std::cout << "LEIJURV: MISMATCH it did stride in units of " << kAllocationStride << " but to a total count of " << allocIdx << " while we expected between 0 and " << allocationSizes.size() << " exclusive" << std::endl;
      return std::nullopt;
    }

    int64_t offset = (int64_t) ptr1 - kAllocationBase;
    if (offset < 0 || offset >= (int64_t)allocationSizes[allocIdx]) {
      std::cout << "LEIJURV: MISMATCH the base offset was " << offset << " while we expected between 0 and " << allocationSizes[allocIdx] << " exclusive" << std::endl;
      return std::nullopt;
    }
    return std::make_tuple(allocIdx, offset);
  };

  for (size_t i = 0; i < num_nodes1; ++i) {
    cudaGraphNode_t node1 = nodes1[i];
    cudaGraphNode_t node2 = nodes2[i];

    cudaGraphNodeType type1, type2;
    AT_CUDA_CHECK(cudaGraphNodeGetType(node1, &type1));
    AT_CUDA_CHECK(cudaGraphNodeGetType(node2, &type2));
    TORCH_CHECK_EQ(type1, type2);

    if (type1 == cudaGraphNodeTypeKernel) {
      cudaKernelNodeParams nodeParams1, nodeParams2;
      AT_CUDA_CHECK(cudaGraphKernelNodeGetParams(node1, &nodeParams1));
      AT_CUDA_CHECK(cudaGraphKernelNodeGetParams(node2, &nodeParams2));
      TORCH_CHECK_EQ(nodeParams1.func, nodeParams2.func);
      cudaFunction_t func;
      AT_CUDA_CHECK(cudaGetFuncBySymbol(&func, nodeParams1.func));

      const char* func_name;
      globalContext().getNVRTC().cuFuncGetName(&func_name, func);

      std::cout << "GALVEZ: kernel name=" << func_name << std::endl;

      size_t param_index = 0;
      size_t param_offset;
      size_t param_size;
      while (globalContext().getNVRTC().cuFuncGetParamInfo(
                 func, param_index, &param_offset, &param_size) !=
             CUDA_ERROR_INVALID_VALUE) {
        char** arg1_speculative_pointer =
            (char**)nodeParams1.kernelParams[param_index];
        char** arg2_speculative_pointer =
            (char**)nodeParams2.kernelParams[param_index];
        // We are assuming that arg1 and arg2 are both pointer types.
        
        for (size_t address_start = 0; param_size - address_start >= 8;
             address_start += 8) {
          
          char* arg1_value = arg1_speculative_pointer[address_start / 8];
          char* arg2_value = arg2_speculative_pointer[address_start / 8];

          if (arg2_value != arg1_value) {
            std::cout << "param_index " << param_index << " param_size " << param_size << " address_start " << address_start << " arg1_value " << (void*) arg1_value << " arg2_value " << (void*)arg2_value << std::endl;
            if (auto result = checkAllocation(arg1_value, arg2_value)) {
              auto [allocIdx, offset] = *result;
              std::cout << "LEIJURV: I have decided that " << address_start << " bytes into argument #" << param_index << " of kernel " << func_name << " is actually allocation " << allocIdx << " indexed to offset " << offset << std::endl;
              cudaKernelNodeAttrValue attr_value = {
                  .deviceUpdatableKernelNode = {
                      .deviceUpdatable = 1,
                      .devNode = nullptr,
                  }
              };
              AT_CUDA_CHECK(cudaGraphKernelNodeSetAttribute(
                  node1,
                  cudaLaunchAttributeDeviceUpdatableKernelNode,
                  &attr_value));
              TORCH_CHECK(
                  attr_value.deviceUpdatableKernelNode.devNode != nullptr);

              kernelParamUpdates.push_back({
                .devNode = attr_value.deviceUpdatableKernelNode.devNode,
                .paramOffset = param_offset + address_start,
                .allocIdx = allocIdx,
                .offset = offset,
              });
            }
          }
        }
        param_index++;
      }

    } else if (type1 == cudaGraphNodeTypeMemcpy) {
      cudaMemcpy3DParms memcpyParams1, memcpyParams2;
      AT_CUDA_CHECK(cudaGraphMemcpyNodeGetParams(node1, &memcpyParams1));
      AT_CUDA_CHECK(cudaGraphMemcpyNodeGetParams(node2, &memcpyParams2));
      TORCH_CHECK(memcpyParams1.extent.width == memcpyParams2.extent.width &&
                  memcpyParams1.extent.height == memcpyParams2.extent.height &&
                  memcpyParams1.extent.depth == memcpyParams2.extent.depth,
                  "Memcpy extent mismatch between graphs");
      TORCH_CHECK(memcpyParams1.kind == memcpyParams2.kind, "Memcpy kind mismatch between graphs");
      if (memcpyParams1.srcPtr.ptr == memcpyParams2.srcPtr.ptr && memcpyParams1.dstPtr.ptr == memcpyParams2.dstPtr.ptr) {
        continue; // unchanged
      }
      auto srcPtrResult = checkAllocation(memcpyParams1.srcPtr.ptr, memcpyParams2.srcPtr.ptr);
      auto dstPtrResult = checkAllocation(memcpyParams1.dstPtr.ptr, memcpyParams2.dstPtr.ptr);
      if (!srcPtrResult && !dstPtrResult) {
        continue; // unable to figure it out
      }
      
      
      graphNodeParamUpdates.push_back({
        .node = node1,
        .computeNewParams = [memcpyParams1, srcPtrResult, dstPtrResult](std::vector<void*> actualDataPtrs) {
          void* srcPtr = memcpyParams1.srcPtr.ptr;
          void* dstPtr = memcpyParams1.dstPtr.ptr;

          if (srcPtrResult) {
            auto [allocIdx, offset] = *srcPtrResult;
            srcPtr = (char*)actualDataPtrs[allocIdx] + offset;
          }
          if (dstPtrResult) {
            auto [allocIdx, offset] = *dstPtrResult;
            dstPtr = (char*)actualDataPtrs[allocIdx] + offset;
          }

          cudaGraphNodeParams nodeParams = {};
          nodeParams.type = cudaGraphNodeTypeMemcpy;
          cudaMemcpy3DParms updatedMemcpyParams = {
            .srcArray = memcpyParams1.srcArray,
            .srcPos = memcpyParams1.srcPos,
            .srcPtr = {srcPtr, memcpyParams1.srcPtr.pitch, memcpyParams1.srcPtr.xsize, memcpyParams1.srcPtr.ysize},
            .dstArray = memcpyParams1.dstArray,
            .dstPos = memcpyParams1.dstPos,
            .dstPtr = {dstPtr, memcpyParams1.dstPtr.pitch, memcpyParams1.dstPtr.xsize, memcpyParams1.dstPtr.ysize},
            .extent = memcpyParams1.extent,
            .kind = memcpyParams1.kind
          };
          nodeParams.memcpy.copyParams = updatedMemcpyParams;
          return nodeParams;
        },
      });
    } else if (type1 == cudaGraphNodeTypeMemset) {
      cudaMemsetParams memsetParams1, memsetParams2;
      AT_CUDA_CHECK(cudaGraphMemsetNodeGetParams(node1, &memsetParams1));
      AT_CUDA_CHECK(cudaGraphMemsetNodeGetParams(node2, &memsetParams2));
      
      TORCH_CHECK(memsetParams1.value == memsetParams2.value, "Memset value mismatch between graphs");
      TORCH_CHECK(memsetParams1.elementSize == memsetParams2.elementSize, "Memset element size mismatch between graphs");
      TORCH_CHECK(memsetParams1.width == memsetParams2.width, "Memset width mismatch between graphs");
      TORCH_CHECK(memsetParams1.height == memsetParams2.height, "Memset height mismatch between graphs");

      if (memsetParams1.dst == memsetParams2.dst) {
        continue; // unchanged
      }
      
      auto dstPtrResult = checkAllocation(memsetParams1.dst, memsetParams2.dst);
      if (!dstPtrResult) {
        continue; // unable to figure it out
      }
      
      graphNodeParamUpdates.push_back({
        .node = node1,
        .computeNewParams = [memsetParams1, dstPtrResult](std::vector<void*> actualDataPtrs) {
          void* dstPtr = memsetParams1.dst;

          if (dstPtrResult) {
            auto [allocIdx, offset] = *dstPtrResult;
            dstPtr = (char*)actualDataPtrs[allocIdx] + offset;
          }

          cudaGraphNodeParams nodeParams = {};
          nodeParams.type = cudaGraphNodeTypeMemset;
          cudaMemsetParamsV2 updatedMemsetParams = {
            .dst = dstPtr,
            .pitch = memsetParams1.pitch,
            .value = memsetParams1.value,
            .elementSize = memsetParams1.elementSize,
            .width = memsetParams1.width,
            .height = memsetParams1.height
          };
          nodeParams.memset = updatedMemsetParams;
          return nodeParams;
        },
      });
    } else if (type1 == cudaGraphNodeTypeGraph || type1 == cudaGraphNodeTypeConditional) {
      throw std::runtime_error("horrible. please don't make me support these.");
      assert(false);
    } else if (
      type1 == cudaGraphNodeTypeEmpty ||
      type1 == cudaGraphNodeTypeWaitEvent ||
      type1 == cudaGraphNodeTypeHost ||
      type1 == cudaGraphNodeTypeEventRecord ||
      type1 == cudaGraphNodeTypeExtSemaphoreSignal ||
      type1 == cudaGraphNodeTypeExtSemaphoreWait ||
      type1 == cudaGraphNodeTypeMemAlloc ||
      type1 == cudaGraphNodeTypeMemFree
    ) {
      // this is fine
    } else {
      throw std::runtime_error("graph node type unknown");
      assert(false);
    }
  }
  instantiate_graph_exec(); // must do this after cudaGraphKernelNodeSetAttribute
  hasComparedAgainstRecapture = true;
}

void CUDART_CB hostMemoryFreeCallback(cudaStream_t stream, cudaError_t error, void* data) {
  at::getCPUAllocator()->raw_deallocate(data);
}

void CUDAGraph::replay_dynamic(std::vector<void*> prefilledDataPtrs, std::vector<size_t> prefilledLens) {
  TORCH_CHECK(hasComparedAgainstRecapture, "Must compare against a pointer offsetted sentinel recapture");
  TORCH_CHECK(has_graph_exec_,
              "Called CUDAGraph::replay without a preceding successful capture.");
  TORCH_CHECK(prefilledDataPtrs.size() <= allocationSizes.size());
  TORCH_CHECK(prefilledLens.size() == prefilledDataPtrs.size());
  c10::OptionalDeviceGuard device_guard{capture_stream_.device()};
  cudaStream_t stream = at::cuda::getCurrentCUDAStream();

  // take the allocations that were requested during the capture, and allocate them "for real"
  std::vector<void*> actualDataPtrs;
  auto allocator = c10::cuda::CUDACachingAllocator::get();
  for (size_t i = 0; i < allocationSizes.size(); i++) {
    if (i < prefilledDataPtrs.size()) {
      // the first few are inputs/outputs, so they are "prefilled"
      TORCH_CHECK(prefilledLens[i] == allocationSizes[i], "Prefilled tensors must be same shape");
      actualDataPtrs.push_back(prefilledDataPtrs[i]);
    } else {
      // the rest are allocated empty
      actualDataPtrs.push_back(allocator->raw_alloc_with_stream(allocationSizes[i], stream));
    }
  }

  for (auto& update : graphNodeParamUpdates) {
    // run the updates that can't be done device-side
    // note that this is quite rare - it only applies to a few misc memsets
    cudaGraphNodeParams newParams = update.computeNewParams(actualDataPtrs);
    AT_CUDA_CHECK(cudaGraphExecNodeSetParams(graph_exec_, update.node, &newParams));
  }

  // practically all of the updates are kernel param updates, which are batched into a single device-side call:
  size_t totalUpdatesSize = kernelParamUpdates.size() * sizeof(cudaGraphKernelNodeUpdate);
  cudaGraphKernelNodeUpdate* hostUpdates = (cudaGraphKernelNodeUpdate*) at::getCPUAllocator()->raw_allocate(totalUpdatesSize);
  for (size_t i = 0; i < kernelParamUpdates.size(); i++) {
    auto update = kernelParamUpdates[i];
    cudaGraphKernelNodeUpdate deviceUpdate = {
      .node = update.devNode,
      .field = cudaGraphKernelNodeFieldParam,
      .updateData = {
        .param = {
          .pValue = (char*)actualDataPtrs[update.allocIdx] + update.offset, // the kernel will overwrite this to indirect it in GPU memory
          .offset = update.paramOffset,
          .size = sizeof(void*),
        }
      }
    };
    hostUpdates[i] = deviceUpdate;
  }
  cudaGraphKernelNodeUpdate* deviceUpdates = (cudaGraphKernelNodeUpdate*) allocator->raw_alloc_with_stream(totalUpdatesSize, stream);
  AT_CUDA_CHECK(cudaMemcpyAsync(deviceUpdates, hostUpdates, totalUpdatesSize, cudaMemcpyHostToDevice, stream));
  AT_CUDA_CHECK(cudaStreamAddCallback(stream, hostMemoryFreeCallback, hostUpdates, 0)); // free once the memcpy is done

  AT_CUDA_CHECK(cudaGraphUpload(graph_exec_, stream));

  dynamicGraphUpdater(deviceUpdates, kernelParamUpdates.size());

  AT_CUDA_CHECK(cudaGraphLaunch(graph_exec_, stream));
  allocator->raw_delete(deviceUpdates);
  for (size_t i = prefilledDataPtrs.size(); i < actualDataPtrs.size(); i++) { // don't free prefilled
    allocator->raw_delete(actualDataPtrs[i]);
  }
}

} // namespace at::cuda
