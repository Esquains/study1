#include <ATen/detail/ComplexHooksInterface.h>

namespace at {

namespace detail {
const ComplexHooksInterface& getComplexHooks() {
  static std::unique_ptr<ComplexHooksInterface> complex_hooks;
  // NB: The once_flag here implies that if you try to call any Complex
  // functionality before you load the complex library, you're toast.
  // Same restriction as in getCUDAHooks()
  static std::once_flag once;
  std::call_once(once, [] {
    complex_hooks = c10::ComplexHooksRegistry()->Create("ComplexHooks", ComplexHooksArgs{});
    if (!complex_hooks) {
      complex_hooks =
          std::unique_ptr<ComplexHooksInterface>(new ComplexHooksInterface());
    }
  });
  return *complex_hooks;
}
} // namespace detail
} // namespace at

C10_DEFINE_REGISTRY(ComplexHooksRegistry, at::ComplexHooksInterface, at::ComplexHooksArgs)
