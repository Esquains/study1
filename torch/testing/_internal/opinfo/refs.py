from torch.testing._internal.opinfo.core import (
    BinaryUfuncInfo,
    OpInfo,
    ReductionOpInfo,
    UnaryUfuncInfo,
)

# NOTE [Python References]
# Python References emulate existing PyTorch operations, but can ultimately
#   be expressed in terms of "primitive" operations from torch._prims.
#
# These references are experimental.
# See https://dev-discuss.pytorch.org/t/tracing-with-primitives-update-0/577
#   for additional context.
#
# Python Reference OpInfos should be added to the python_ref_db list below.
#   Tests can opt-into running on these references by including
#   that list in the Sequence they pass to the @ops decorator.
#
# When a Python Reference OpInfo is constructed a pointer to an
#   existing OpInfo must be provided using the torch_opinfo_name kwarg.
#   The existing OpInfo with that name and no variant will be found
#   to inherit from.
#
# Instead of just inheriting the existing OpInfo's metadata, the
#   Python Reference OpInfos inherit the existing OpInfo's
#   construction arguments. These arguments can be overridden
#   by adding kwargs to the constructor.


def _find_referenced_opinfo(referenced_name, variant_name, *, op_db=None):
    """
    Finds the OpInfo with the given name that has no variant name.
    """
    # NOTE: searching the global op_db doesn't work when OpInfos are split into
    # different modules, as otherwise the op_db will not be fully constructed
    # yet. So, instead the local op_db must be passed in explicitly.
    if op_db is None:
        from torch.testing._internal.common_methods_invocations import op_db

    for opinfo in op_db:
        if opinfo.name == referenced_name and opinfo.variant_test_name == variant_name:
            return opinfo


def _inherit_constructor_args(name, op, inherited, overrides):
    # inherits metadata
    common_kwargs = {
        "name": name,
        "op": op,
        "aliases": None,  # TODO add a check for alias coverage
        "method_variant": None,
        "inplace_variant": None,  # TODO: add a check for inplace coverage
        "supports_scripting": False,
    }

    # Acquires inherited kwargs
    kwargs = inherited.copy()

    # Fixes metadata
    if "kwargs" in kwargs:
        kwargs.update(kwargs["kwargs"])
        del kwargs["kwargs"]
    if "self" in kwargs:
        del kwargs["self"]
    if "__class__" in kwargs:
        del kwargs["__class__"]
    if "skips" in kwargs:
        del kwargs["skips"]
    if "decorators" in kwargs:
        del kwargs["decorators"]

    # Overrides metadata
    kwargs.update(common_kwargs)
    kwargs.update(overrides)

    # At the moment no prims support autograd, so we must not run autograd
    # tests e.g. when testing dtype support.  Once we start writing autograd
    # formulas for prims this can be removed.
    kwargs["supports_autograd"] = False
    kwargs["supports_gradgrad"] = False
    kwargs["supports_fwgrad_bwgrad"] = False
    kwargs["supports_inplace_autograd"] = False
    kwargs["supports_forward_ad"] = False

    return kwargs


def _test_ref_export(name):
    """
    Make sure 'name' is exported via '__all__'.
    """
    # Note: we need to import this because 'torch.ops' modules are not
    # initialized until 'torch._prims' is imported.  We also cannot use
    # '__import__' or 'importlib.import_module' and need to walk the
    # module hierarchy manually.
    import torch

    lst = name.split(".")
    mod_name = lst[:-1]
    func_name = lst[-1]

    mod = torch
    for i in range(len(mod_name)):
        mod = getattr(mod, mod_name[i])

    # ops.nvprims doesn't have __all__
    if hasattr(mod, "__all__"):
        assert func_name in mod.__all__, f"missing ref export: {name}"


class PythonRefInfo(OpInfo):
    """
    An OpInfo for a Python reference of an OpInfo base class operation.
    """

    def __init__(
        self,
        name,  # the stringname of the callable Python reference
        *,
        op=None,  # the function variant of the operation, populated as torch.<name> if None
        op_db=None,  # The database of opinfos to search for the parent opinfo
        torch_opinfo_name,  # the string name of the corresponding torch opinfo
        torch_opinfo_variant_name="",  # the variant name for corresponding torch opinfo
        validate_view_consistency=True,
        supports_nvfuser=True,
        **kwargs,
    ):  # additional kwargs override kwargs inherited from the torch opinfo

        self.torch_opinfo_name = torch_opinfo_name
        self.torch_opinfo_variant_name = torch_opinfo_variant_name
        self.torch_opinfo = _find_referenced_opinfo(
            torch_opinfo_name, torch_opinfo_variant_name, op_db=op_db
        )
        self.validate_view_consistency = validate_view_consistency
        self.supports_nvfuser = supports_nvfuser
        assert isinstance(self.torch_opinfo, OpInfo)

        _test_ref_export(name)

        inherited = self.torch_opinfo._original_opinfo_args
        ukwargs = _inherit_constructor_args(name, op, inherited, kwargs)
        super(PythonRefInfo, self).__init__(**ukwargs)


class ReductionPythonRefInfo(ReductionOpInfo):
    """
    An OpInfo for a Python reference of an elementwise unary operation.
    """

    def __init__(
        self,
        name,  # the stringname of the callable Python reference
        *,
        op=None,  # the function variant of the operation, populated as torch.<name> if None
        op_db=None,  # The database of opinfos to search for the parent opinfo
        torch_opinfo_name,  # the string name of the corresponding torch opinfo
        torch_opinfo_variant_name="",  # the variant name for corresponding torch opinfo
        supports_nvfuser=True,
        **kwargs,
    ):  # additional kwargs override kwargs inherited from the torch opinfo

        self.torch_opinfo_name = torch_opinfo_name
        self.torch_opinfo_variant_name = torch_opinfo_variant_name
        self.torch_opinfo = _find_referenced_opinfo(
            torch_opinfo_name, torch_opinfo_variant_name, op_db=op_db
        )
        self.supports_nvfuser = supports_nvfuser
        assert isinstance(self.torch_opinfo, ReductionOpInfo)

        _test_ref_export(name)

        inherited = self.torch_opinfo._original_reduction_args
        ukwargs = _inherit_constructor_args(name, op, inherited, kwargs)

        # See https://github.com/pytorch/pytorch/issues/77216
        self.validate_view_consistency = False

        super().__init__(**ukwargs)


class ElementwiseUnaryPythonRefInfo(UnaryUfuncInfo):
    """
    An OpInfo for a Python reference of an elementwise unary operation.
    """

    def __init__(
        self,
        name,  # the stringname of the callable Python reference
        *,
        op=None,  # the function variant of the operation, populated as torch.<name> if None
        op_db=None,  # The database of opinfos to search for the parent opinfo
        torch_opinfo_name,  # the string name of the corresponding torch opinfo
        torch_opinfo_variant_name="",  # the variant name for corresponding torch opinfo
        supports_nvfuser=True,
        **kwargs,
    ):  # additional kwargs override kwargs inherited from the torch opinfo

        self.torch_opinfo_name = torch_opinfo_name
        self.torch_opinfo_variant_name = torch_opinfo_variant_name
        self.torch_opinfo = _find_referenced_opinfo(
            torch_opinfo_name, torch_opinfo_variant_name, op_db=op_db
        )
        self.supports_nvfuser = supports_nvfuser
        assert isinstance(self.torch_opinfo, UnaryUfuncInfo)

        _test_ref_export(name)

        inherited = self.torch_opinfo._original_unary_ufunc_args
        ukwargs = _inherit_constructor_args(name, op, inherited, kwargs)

        super(ElementwiseUnaryPythonRefInfo, self).__init__(**ukwargs)


class ElementwiseBinaryPythonRefInfo(BinaryUfuncInfo):
    """
    An OpInfo for a Python reference of an elementwise binary operation.
    """

    def __init__(
        self,
        name,  # the stringname of the callable Python reference
        *,
        op=None,  # the function variant of the operation, populated as torch.<name> if None
        op_db=None,  # The database of opinfos to search for the parent opinfo
        torch_opinfo_name,  # the string name of the corresponding torch opinfo
        torch_opinfo_variant_name="",  # the variant name for corresponding torch opinfo
        supports_nvfuser=True,
        **kwargs,
    ):  # additional kwargs override kwargs inherited from the torch opinfo

        self.torch_opinfo_name = torch_opinfo_name
        self.torch_opinfo_variant_name = torch_opinfo_variant_name
        self.torch_opinfo = _find_referenced_opinfo(
            torch_opinfo_name, torch_opinfo_variant_name, op_db=op_db
        )
        self.supports_nvfuser = supports_nvfuser
        assert isinstance(self.torch_opinfo, BinaryUfuncInfo)

        _test_ref_export(name)

        inherited = self.torch_opinfo._original_binary_ufunc_args
        ukwargs = _inherit_constructor_args(name, op, inherited, kwargs)

        super(ElementwiseBinaryPythonRefInfo, self).__init__(**ukwargs)
