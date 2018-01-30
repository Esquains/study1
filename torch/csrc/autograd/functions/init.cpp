#include "Python.h"
#include "accumulate_grad.h"
#include "basic_ops.h"
#include "tensor.h"
#include "special.h"
#include "torch/csrc/jit/interpreter_autograd_function.h"
#include "torch/csrc/autograd/functions/pybind.h"
#include "torch/csrc/autograd/python_cpp_function.h"
#include "torch/csrc/autograd/generated/python_functions.h"
#include "torch/csrc/jit/python_tracer.h"
#include "torch/csrc/utils/pybind.h"
#include "torch/csrc/utils/tuple_parser.h"

using namespace torch::autograd;
using torch::TupleParser;

struct DelayedErrorCtor {
  DelayedError* operator()(PyObject* args) {
    std::string msg;

    TupleParser parser(args, 1);
    parser.parse(msg, "msg");

    return new DelayedError(msg);
  }
};

struct NoCtor {
  Function* operator()(PyObject* args) {
    throw std::runtime_error("Cannot construct");
  }
};

template<typename C, typename T>
static void addClass(PyObject* module, PyTypeObject& type, const char* name,
  PyGetSetDef* function_properties=nullptr, PyMethodDef* function_methods=nullptr)
{
  createForwardFunctionPyTypeObject<T>(type, name, function_properties, function_methods);
  Py_INCREF(&type);
  PyModule_AddObject(module, name, (PyObject*)&type);
  registerCppFunction(typeid(C), &type);
}

template<typename T, typename ValueT, typename ParamsT, ValueT ParamsT::*ptr,
         typename ConvertArgT, PyObject* (*Convert)(ConvertArgT)>
PyObject* getTupleAttr(PyObject* obj, void* _unused)
{
  HANDLE_TH_ERRORS
  THPCppFunction* self = (THPCppFunction*)obj;
  auto& arr = ((T*)(self->cdata.get()))->*ptr;
  auto num_elems = arr.size();
  THPObjectPtr py_tuple(PyTuple_New(num_elems));
  if (!py_tuple) return nullptr;
  for (size_t i = 0; i < num_elems; ++i) {
    PyTuple_SET_ITEM(py_tuple.get(), i, Convert(arr[i]));
  }
  return py_tuple.release();
  END_HANDLE_TH_ERRORS
}

template<typename T, typename ValueT, typename ParamsT, ValueT ParamsT::*ptr,
         typename ConvertArgT, PyObject* (*Convert)(ConvertArgT)>
PyObject* getValueAttr(PyObject* obj, void* _unused)
{
  HANDLE_TH_ERRORS
  THPCppFunction* self = (THPCppFunction*)obj;
  auto& val = ((T*)(self->cdata.get()))->*ptr;
  return Convert(val);
  END_HANDLE_TH_ERRORS
}

static PyObject* accumulateGradVar(PyObject *_self, void* _unused)
{
  THPCppFunction* self = (THPCppFunction*)_self;
  auto grad_acc = (AccumulateGrad*)self->cdata.get();
  return THPVariable_Wrap(grad_acc->variable);
}

static struct PyGetSetDef accumulate_grad_properties[] = {
  THP_FUNCTION_DEFAULT_PROPERTIES,
  {(char*)"variable", accumulateGradVar, nullptr, nullptr, nullptr},
  {nullptr}
};

bool THPAutograd_initFunctions(PyObject* _unused)
{
  THPObjectPtr module(PyModule_New("torch._C._functions"));
  if (!module) return false;

  static PyTypeObject AccumulateGradClass;
  addClass<AccumulateGrad, NoCtor>(module, AccumulateGradClass, "AccumulateGrad", accumulate_grad_properties);

  static PyTypeObject ErrorClass;
  addClass<Error, NoCtor>(module, ErrorClass, "Error");

  static PyTypeObject DelayedErrorClass;
  addClass<DelayedError, DelayedErrorCtor>(module, DelayedErrorClass, "DelayedError");

  static PyTypeObject EvalClass;
  addClass<Eval, NoCtor>(module, EvalClass, "Eval");

  static PyTypeObject InterpreterAutogradClass;
  addClass<torch::jit::InterpreterAutogradFunction, NoCtor>(module, InterpreterAutogradClass, "InterpreterAutogradFunction");

  static PyTypeObject CopyBackwardsClass;
  addClass<CopyBackwards, NoCtor>(module, CopyBackwardsClass, "CopyBackwards");

  static PyTypeObject CopySlicesClass;
  addClass<CopySlices, NoCtor>(module, CopySlicesClass, "CopySlices");

  generated::initialize_autogenerated_functions();

  THPObjectPtr parent(PyImport_ImportModule("torch._C"));
  if (!parent) return false;
  PyModule_AddObject(parent.get(), "_functions", module.release());
  return true;
}

namespace torch { namespace autograd {

void initAutogradClosureBindings(PyObject* module) {
  auto m = py::handle(module).cast<py::module>();
  py::class_<jit::InterpreterFunctionFactory,std::shared_ptr<jit::InterpreterFunctionFactory>>(m, "InterpreterFunctionFactory")
    .def("__call__", &jit::InterpreterFunctionFactory::construct_function)
    ;

  m.def("_jit_createInterpreterFactory", [](jit::tracer::TracingState* tracing_state) {
    return std::make_shared<jit::InterpreterFunctionFactory>(tracing_state);
  });
}

}}
