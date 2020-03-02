import torch
import functools
import warnings


class autocast(object):
    r"""
    Instances of :class:`autocast` serve as context managers or decorators that
    allow regions of your script to run in mixed precision.

    Within autocast-enabled regions, the backend automatically chooses the precision
    for GPU operations to improve performance while maintaining accuracy.

    When entering an autocast-enabled region, Tensors may be any type.  It is not necessary or
    recommended to call ``.half()`` on your model or data to use autocasting.

    :class:`autocast` should wrap the forward pass of your model::

        # Creates model in default precision (float32)
        model = Net().cuda()

        for input, target in data:
            optimizer.zero_grad()

            # Enables autocasting for the forward pass (model + loss)
            with autocast():
                output = model(input)
                loss = loss_fn(output, target)

            # Exits the context manager before backward()
            # Running backward() under autocast is not necessary or recommended.
            # Backward ops run in the same precision that autocast used for corresponding forward ops.
            loss.backward()
            optimizer.step()

    :class:`autocast` can also be used as a decorator, e.g., on the ``forward`` method of your model::

    class AutocastModel(nn.Module):
        ...
        @autocast()
        def forward(self, input):
            ...

    :class:`autocast` is nestable.  If you want to force particular ops to run in ``float32``,
    you can nest ``autocast(enabled=False)`` regions within a surrounding autocast-enabled region::

        mat0 = torch.rand((8,8), device="cuda", dtype.torch.float32)
        mat1 = torch.rand((8,8), device="cuda", dtype.torch.float32)
        mat2 = torch.rand((8,8), device="cuda", dtype.torch.float32)
        mat3_float16 = torch.rand((8,8), device="cuda", dtype.torch.float16)

        with autocast():
            # torch.mm is on autocast's list of ops that should run in float16..
            # Although inputs are float32, the op runs in float16 and produces float16 output.
            # No manual casts are required.
            tmp_float16 = torch.mm(mat0, mat1)

            with autocast(enabled=False):
                # Here torch.mm does not autocast.
                # To force float32 execution, ensure the inputs are float32.
                # The output type matches the input types.
                tmp_float32 = torch.mm(tmp_float16.float(), mat2)

            # No manual casts are required when re-entering the autocast-enabled region.
            # torch.mm again runs in float16 and produces float16 output, regardless of input types.
            # Note that mismatched input types are transparently handled.
            float16_result = torch.mm(tmp_float32, mat3_float16)


    Arguments:
        enabled(bool, optional, default=True):  Whether autocasting should be enabled within this region.

    .. note::
        Tensors produced in an autocast-enabled region may be ``float16``.  After returning to an
        autocast-disabled region, using them along with ``float32`` tensors may cause type mismatch errors.
        If so, simply call ``.float()`` on the offending tensor(s).

        Type mismatch errors *within* an autocast-enabled region are a bug; if this is what you observe,
        please file an issue.

    .. note::
        Autocast only affects GPU operations (operations running on CUDA Tensors).

    .. note::
        The autocast state is thread-local.  If you want it enabled in a new thread, the context manager or decorator
        must be invoked in that thread.  This affects :class:`torch.nn.DataParallel`, which spawns
        new threads to run ``forward`` on each device.  See the :ref:`DataParallel example<amp-dataparallel`
        for best practices.

    .. note::
        Currently, autocast only affects out-of-place operations.  In-place ops still work in autocast-enabled
        regions, but won't be autocasted (e.g., ``torch.addmm`` is guaranteed to run in ``float16``, but
        ``torch.addmm_`` may not).  For best performance and accuracy, prefer out-of-place ops if possible.
    """
    def __init__(self, enabled=True):
        if enabled and not torch.cuda.is_available():
            warnings.warn("torch.cuda.amp.autocast only affects CUDA ops, but CUDA is not available.  Disabling.")
            self._enabled = False
        else:
            self._enabled = enabled

    def __enter__(self):
        self.prev = torch.is_autocast_enabled()
        torch.set_autocast_enabled(self._enabled)
        torch.autocast_increment_nesting()

    def __exit__(self, *args):
        # Drop the cache when we exit to a nesting level that's outside any instance of autocast.
        if torch.autocast_decrement_nesting() == 0:
            torch.clear_autocast_cache()
        torch.set_autocast_enabled(self.prev)
        return False

    def __call__(self, func):
        @functools.wraps(func)
        def decorate_autocast(*args, **kwargs):
            with self:
                return func(*args, **kwargs)
        return decorate_autocast
