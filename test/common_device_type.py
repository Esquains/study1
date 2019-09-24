import inspect
from functools import wraps
import unittest
import torch
from common_utils import TestCase, TEST_WITH_ROCM, TEST_MKL, \
    skipCUDANonDefaultStreamIf

# Note: Generic Device-Type Testing
#
# [WRITING TESTS]
#
# Write your test class as usual except:
#   (1) Each test method should have one of four signatures:
#
#           (1a) testX(self, device)
#
#           (1b) @multidevice(<minimum number of devices to run test with>)
#                testX(self, devices)
#
#           (1c) @dtypes(<list of dtypes>)
#                testX(self, device, dtype)
#
#           (1d) @multidevice(<minimum number of devices to run test with>)
#                @dtypes(<list of dtypes>)
#                testX(self, devices, dtype)
#
#
#       Note that the decorators are required for signatures (1b), (1c) and
#       (1d).
#
#       When a test like (1a) is called it will be given a device string,
#       like 'cpu' or 'cuda:0.'
#
#       Tests like (1b) are called with a list of device strings, like
#       ['cuda:0', 'cuda:1']. The first device string will be the
#       current/default device. These tests will be skipped if the device type
#       has fewer available devices than the argument to @multidevice.
#
#       Tests like (1c) are called with a device string and a torch.dtype from
#       the list of dtypes specified in the @dtypes decorator. Device-specific
#       dtype overrides can be specified using @dtypesIfCPU and @dtypesIfCUDA.
#
#       Tests like (1d) take a devices argument like (1b) and a dtype
#       argument from (1c).
#
#   (2) Prefer using test decorators defined in this file to others.
#       For example, using the @skipIfNoLapack decorator instead of the
#       @skipCPUIfNoLapack will cause the test to not run on CUDA if
#       LAPACK is not available, which is wrong. If you need to use a decorator
#       you may want to ask about porting it to this framework.
#
#   See the TestTorchDeviceType class in test_torch.py for an example.
#
# [RUNNING TESTS]
#
# After defining your test class call instantiate_device_type_tests on it
# and pass in globals() for the second argument. This will instantiate
# discoverable device-specific test classes from your generic class. It will
# also hide the tests in your generic class so they're not run.
#
# If you device-generic test class is TestClass then new classes with names
# TestClass<DEVICE_TYPE> will be created for each available device type.
# TestClassCPU and TestClassCUDA, for example. Tests in these classes also
# have the device type and dtype, if provided, appended to their original
# name. testX, for instance, becomes testX_<device_type> or
# testX_<device_type>_<dtype>.
#
# More concretely, TestTorchDeviceType becomes TestTorchDeviceTypeCPU,
# TestTorchDeviceTypeCUDA, ... test_diagonal in TestTorchDeviceType becomes
# test_diagonal_cpu, test_diagonal_cuda, ... test_erfinv, which accepts a dtype,
# becomes test_erfinv_cpu_float, test_erfinv_cpu_double, test_erfinv_cuda_half,
# ...
#
# In short, if you write a test signature like
#   def textX(self, device)
# You are effectively writing
#   def testX_cpu(self, device='cpu')
#   def textX_cuda(self, device='cuda')
#   def testX_xla(self, device='xla')
#   ...
#
# These tests can be run directly like normal tests:
# "python test_torch.py TestTorchDeviceTypeCPU.test_diagonal_cpu"
#
# All the tests for a particular device type can be run using the class, and
# other collections of tests can be run using pytest filtering, like
#
# "pytest test_torch.py -k 'test_diag'"
#
# which will run test_diag on every available device.
#
# To specify particular device types the 'and' keyword can be used:
#
# "pytest test_torch.py -k 'test_erfinv and cpu'"
#
# will run test_erfinv on all cpu dtypes.
#
# [ADDING A DEVICE TYPE]
#
# To add a device type:
#
#   (1) Create a new "TestBase" extending DeviceTypeTestBase.
#       See CPUTestBase and CUDATestBase below.
#   (2) Define the "device_type" attribute of the base to be the
#       appropriate string.
#   (3) Add logic to this file that appends your base class to
#       device_type_test_bases when your device type is available.
#   (4) (Optional) Write setUpClass/tearDownClass class methods that
#       instantiate dependencies (see MAGMA in CUDATestBase).
#   (5) (Optional) Override the "instantiate_test" method for total
#       control over how your class creates tests.
#
# setUpClass is called AFTER tests have been created and BEFORE and ONLY IF
# they are run. This makes it useful for initializing devices and dependencies.
#

# List of device type test bases that can be used to instantiate tests.
# See below for how this list is populated. If you're adding a device type
# you should check if it's available and (if it is) add it to this list.
device_type_test_bases = []


class DeviceTypeTestBase(TestCase):
    device_type = 'generic_device_type'
    default_device = device_type  # Note: can be defined in setUpClass

    # Returns a string representing the default device tests should use.
    # Note: single device tests use this device exclusively.
    @classmethod
    def get_default_device(cls):
        return cls.default_device

    # Returns a list of strings representing all available devices of this
    # device type. The default device must be the first string in the list
    # and the list must contain no duplicates.
    # Note: UNSTABLE API. Will be replaced once PyTorch has a device generic
    #   mechanism of acquiring all available devices.
    @classmethod
    def get_all_devices(cls):
        return [cls.get_default_device()]

    # Returns the dtypes the test has requested.
    # Prefers device-specific dtype specifications over generic ones.
    @classmethod
    def _get_dtypes(cls, test):
        if not hasattr(test, 'dtypes'):
            return None
        return test.dtypes.get(cls.device_type, test.dtypes.get('all', None))

    # Creates device-specific tests.
    @classmethod
    def instantiate_test(cls, test):
        test_name = test.__name__ + "_" + cls.device_type

        dtypes = cls._get_dtypes(test)
        if dtypes is None:  # Test has no dtype variants
            assert not hasattr(cls, test_name), "Redefinition of test {0}".format(test_name)

            @wraps(test)
            def instantiated_test(self, test=test):
                device_arg = cls.get_default_device() if not hasattr(test, 'deviceCountAtLeast') else cls.get_all_devices()
                return test(self, device_arg)

            setattr(cls, test_name, instantiated_test)
        else:  # Test has dtype variants
            for dtype in dtypes:
                dtype_str = str(dtype).split('.')[1]
                dtype_test_name = test_name + "_" + dtype_str
                assert not hasattr(cls, dtype_test_name), "Redefinition of test {0}".format(dtype_test_name)

                @wraps(test)
                def instantiated_test(self, test=test, dtype=dtype):
                    device_arg = cls.get_default_device() if not hasattr(test, 'deviceCountAtLeast') else cls.get_all_devices()
                    return test(self, device_arg, dtype)

                setattr(cls, dtype_test_name, instantiated_test)


class CPUTestBase(DeviceTypeTestBase):
    device_type = 'cpu'
    default_device = device_type


class CUDATestBase(DeviceTypeTestBase):
    device_type = 'cuda'
    _do_cuda_memory_leak_check = True
    _do_cuda_non_default_stream = True

    @classmethod
    def get_all_devices(cls):
        default_device_idx = int(cls.get_default_device().split(':')[1])
        num_devices = torch.cuda.device_count()

        devices = [cls.get_default_device()]
        cuda_str = 'cuda:{0}'
        non_default_devices = [cuda_str.format(idx) for idx in range(num_devices) if idx != default_device_idx]
        devices.extend(non_default_devices)
        return devices

    @classmethod
    def setUpClass(cls):
        # has_magma shows up after cuda is initialized
        torch.ones(1).cuda()
        cls.no_magma = not torch.cuda.has_magma

        # Acquires the current device as the default (test) device
        cls.default_device = 'cuda:{0}'.format(torch.cuda.current_device())
        torch.cuda.set_device(cls.get_default_device())

    @classmethod
    def tearDownClass(cls):
        torch.cuda.set_device(cls.get_default_device())


# Adds available device-type-specific test base classes
device_type_test_bases.append(CPUTestBase)
if torch.cuda.is_available():
    device_type_test_bases.append(CUDATestBase)


# Adds 'instantiated' device-specific test cases to the given scope.
# The tests in these test cases are derived from the generic tests in
# generic_test_class.
# See note "Generic Device Type Testing."
def instantiate_device_type_tests(generic_test_class, scope):
    # Removes the generic test class from its enclosing scope so its tests
    # are not discoverable.
    del scope[generic_test_class.__name__]

    # Creates an 'empty' version of the generic_test_class
    # Note: we don't inherit from the generic_test_class directly because
    #   that would add its tests to our test classes and they would be
    #   discovered (despite not being runnable). Inherited methods also
    #   can't be removed later, and we can't rely on load_tests because
    #   pytest doesn't support it (as of this writing).
    empty_name = generic_test_class.__name__ + "_base"
    empty_class = type(empty_name, generic_test_class.__bases__, {})

    # Acquires members names
    generic_members = set(dir(generic_test_class)) - set(dir(empty_class))
    generic_tests = [x for x in generic_members if x.startswith('test')]

    # Creates device-specific test cases
    for base in device_type_test_bases:
        class_name = generic_test_class.__name__ + base.device_type.upper()
        device_type_test_class = type(class_name, (base, empty_class), {})

        for name in generic_members:
            if name in generic_tests:  # Instantiates test member

                # Requires tests be a function for Python2 compat
                # (In Python2 tests are type checked methods wrapping functions)
                test = getattr(generic_test_class, name)
                if hasattr(test, '__func__'):
                    test = test.__func__
                assert inspect.isfunction(test), "Couldn't extract function from '{0}'".format(name)

                # Instantiates the device-specific tests
                device_type_test_class.instantiate_test(test)
            else:  # Ports non-test member
                assert not hasattr(device_type_test_class, name), "Redefinition of non-test member {0}".format(name)

                # Unwraps to functions (when available) for Python2 compat
                nontest = getattr(generic_test_class, name)
                if hasattr(nontest, '__func__'):
                    nontest = nontest.__func__

                setattr(device_type_test_class, name, nontest)

        # Mimics defining the instantiated class in the caller's file
        # by setting its module to the given class's and adding
        # the module to the given scope.
        # This lets the instantiated class be discovered by unittest.
        device_type_test_class.__module__ = generic_test_class.__module__
        scope[class_name] = device_type_test_class


# Decorator that skips a test if the given condition is true.
# Notes:
#   (1) Skip conditions stack.
#   (2) Skip conditions can be bools or strings. If a string the
#       test base must have defined the corresponding attribute to be False
#       for the test to run. If you want to use a string argument you should
#       probably define a new decorator instead (see below).
#   (3) Prefer the existing decorators to defining the 'device_type' kwarg.
class skipIf(object):

    def __init__(self, dep, reason, device_type=None):
        self.dep = dep
        self.reason = reason
        self.device_type = device_type

    def __call__(self, fn):

        @wraps(fn)
        def dep_fn(slf, device, *args, **kwargs):
            if self.device_type is None or self.device_type == slf.device_type:
                if (isinstance(self.dep, str) and getattr(slf, self.dep, True)) or (isinstance(self.dep, bool) and self.dep):
                    raise unittest.SkipTest(self.reason)

            return fn(slf, device, *args, **kwargs)
        return dep_fn


# Skips a test on CPU if the condition is true.
class skipCPUIf(skipIf):

    def __init__(self, dep, reason):
        super(skipCPUIf, self).__init__(dep, reason, device_type='cpu')


# Skips a test on CUDA if the condition is true.
class skipCUDAIf(skipIf):

    def __init__(self, dep, reason):
        super(skipCUDAIf, self).__init__(dep, reason, device_type='cuda')


class onlyOn(object):

    def __init__(self, device_type):
        self.device_type = device_type

    def __call__(self, fn):

        @wraps(fn)
        def only_fn(slf, device, *args, **kwargs):
            if self.device_type != slf.device_type:
                reason = "Only runs on {0}".format(self.device_type)
                raise unittest.SkipTest(reason)

            return fn(slf, device, *args, **kwargs)

        return only_fn


# Decorator that provides all available devices of the device type to the test
# as a list of strings instead of providing a single device string.
# Skips the test if the number of available devices of the variant's device
# type is less than the 'deviceCountAtLeast' arg.
class multidevice(object):

    def __init__(self, deviceCountAtLeast):
        self.deviceCountAtLeast = deviceCountAtLeast

    def __call__(self, fn):
        assert not hasattr(fn, 'deviceCountAtLeast'), "multidevice redefinition for {0}".format(fn.__name__)
        fn.deviceCountAtLeast = self.deviceCountAtLeast

        @wraps(fn)
        def multi_fn(slf, devices, *args, **kwargs):
            if len(devices) < self.deviceCountAtLeast:
                reason = "fewer than {0} devices detected".format(self.deviceCountAtLeast)
                raise unittest.SkipTest(reason)

            return fn(slf, devices, *args, **kwargs)

        return multi_fn


# Decorator that instantiates a variant of the test for each given dtype.
# Notes:
#   (1) Tests that accept the dtype argument MUST use this decorator.
#   (2) Can be overriden for the CPU or CUDA, respectively, using dtypesIfCPU
#       or dtypesIfCUDA.
#   (3) Prefer the existing decorators to defining the 'device_type' kwarg.
class dtypes(object):

    # Note: *args, **kwargs for Python2 compat.
    # Python 3 allows (self, *args, device_type='all').
    def __init__(self, *args, **kwargs):
        assert args is not None and len(args) != 0, "No dtypes given"
        assert all(isinstance(arg, torch.dtype) for arg in args), "Unknown dtype in {0}".format(str(args))
        self.args = args
        self.device_type = kwargs.get('device_type', 'all')

    def __call__(self, fn):
        d = getattr(fn, 'dtypes', {})
        assert self.device_type not in d, "dtypes redefinition for {0}".format(self.device_type)
        d[self.device_type] = self.args
        fn.dtypes = d
        return fn


# Overrides specified dtypes on the CPU.
class dtypesIfCPU(dtypes):

    def __init__(self, *args):
        super(dtypesIfCPU, self).__init__(*args, device_type='cpu')


# Overrides specified dtypes on CUDA.
class dtypesIfCUDA(dtypes):

    def __init__(self, *args):
        super(dtypesIfCUDA, self).__init__(*args, device_type='cuda')


def onlyCPU(fn):
    return onlyOn('cpu')(fn)


def onlyCUDA(fn):
    return onlyOn('cuda')(fn)


# Skips a test on CPU if LAPACK is not available.
def skipCPUIfNoLapack(fn):
    return skipCPUIf(not torch._C.has_lapack, "PyTorch compiled without Lapack")(fn)


# Skips a test on CPU if MKL is not available.
def skipCPUIfNoMkl(fn):
    return skipCPUIf(not TEST_MKL, "PyTorch is built without MKL support")(fn)


# Skips a test on CUDA if MAGMA is not available.
def skipCUDAIfNoMagma(fn):
    return skipCUDAIf('no_magma', "no MAGMA library detected")(skipCUDANonDefaultStreamIf(True)(fn))


# Skips a test on CUDA when using ROCm.
def skipCUDAIfRocm(fn):
    return skipCUDAIf(TEST_WITH_ROCM, "test doesn't currently work on the ROCm stack")(fn)
