#include <pybind11/pybind11.h>
#include <torch/csrc/autograd/utils/python_arg_parsing.h>
#include <torch/csrc/nested_tensor/dispatch.h>
#include <torch/csrc/nested_tensor/python_nested_tensor.h>
#include <torch/csrc/utils/cuda_lazy_init.h>
#include <torch/csrc/utils/python_strings.h>

namespace torch {
namespace nested_tensor {

using namespace at;
using namespace torch::autograd;
using namespace torch::autograd::utils;
namespace py = pybind11;

inline PyObject* wrap_list(std::vector<PyObject*> list) {
  auto r = THPObjectPtr{PyTuple_New(list.size())};
  if (!r)
    throw python_error();
  for (size_t i = 0; i < list.size(); ++i) {
    PyTuple_SET_ITEM(r.get(), i, list[i]);
  }
  return r.release();
}

inline PyObject* wrap_nt(
    torch::nested_tensor::_ListNestedTensor nested_tensor) {
  // TODO: Necessary to create new object?
  // What about copy behavior?
  return _ListNestedTensorVariable_Wrap(
      torch::nested_tensor::_ListNestedTensor(nested_tensor));
}

PyObject* _ListNestedTensorVariableClass = nullptr;

PyObject* _ListNestedTensorVariable_nested_size(PyObject* self_) {
  auto& self = reinterpret_cast<_ListNestedTensorVariable*>(self_)->cdata;
  if (self.nested_dim() == 0) {
    return PyTuple_New(0);
  }
  return map_more<PyObject*>(
      self.get_structure(),
      [](at::Tensor tensor) -> PyObject* { return wrap(tensor.sizes()); },
      [](std::vector<PyObject*> list) -> PyObject* { return wrap_list(list); });
}

PyObject* _ListNestedTensorVariable_nested_stride(PyObject* self_) {
  auto& self = reinterpret_cast<_ListNestedTensorVariable*>(self_)->cdata;
  if (self.nested_dim() == 0) {
    return PyTuple_New(0);
  }
  return map_more<PyObject*>(
      self.get_structure(),
      [](at::Tensor tensor) -> PyObject* { return wrap(tensor.strides()); },
      [](std::vector<PyObject*> list) -> PyObject* { return wrap_list(list); });
}

PyObject* _ListNestedTensorVariable_to(
    PyObject* self_,
    PyObject* args,
    PyObject* kwargs) {
  auto parsed = parse_to_conversion(args, kwargs, /*allow_copy*/ true);
  auto& device = std::get<0>(parsed);
  auto& scalarType = std::get<1>(parsed);
  auto non_blocking = std::get<2>(parsed);
  auto copy = std::get<3>(parsed);
  auto opt_memory_format = std::get<4>(parsed);
  auto& self = reinterpret_cast<_ListNestedTensorVariable*>(self_)->cdata;
  if (device && device->is_cuda()) {
    torch::utils::cuda_lazy_init();
  }
  if (!device && !scalarType && !copy) {
    Py_INCREF(self_);
    return self_;
  } else if (!device) {
    return _ListNestedTensorVariable_Wrap(
        self.to(scalarType.value(), non_blocking, copy, opt_memory_format));
  } else if (!scalarType) {
    return _ListNestedTensorVariable_Wrap(self.to(
        self.options().device(device), non_blocking, copy, opt_memory_format));
  } else {
    return _ListNestedTensorVariable_Wrap(self.to(
        device.value(),
        scalarType.value(),
        non_blocking,
        copy,
        opt_memory_format));
  }
  Py_RETURN_NONE;
}

PyObject* _ListNestedTensorVariable_unbind(PyObject* self_) {
  auto& self = reinterpret_cast<_ListNestedTensorVariable*>(self_)->cdata;
  auto children = self.get_structure()._children;
  PyObject* return_list = PyList_New(children.size());
  if (return_list == NULL) {
    throw python_error();
  }
  for (size_t i = 0; i < children.size(); i++) {
    if (children[i]._children.size() == 0) {
      if (PyList_SetItem(
              return_list,
              i,
              THPVariable_Wrap(children[i]._variable_node._variable)) == -1) {
        throw python_error();
      }
    } else {
      if (PyList_SetItem(
              return_list,
              i,
              _ListNestedTensorVariable_Wrap(_ListNestedTensor(children[i]))) ==
          -1) {
        throw python_error();
      }
    }
  }
  return return_list;
}

static void _ListNestedTensorVariable_dealloc(_ListNestedTensorVariable* self) {
  // TODO: Need to revisit this for GC
  // PyObject_GC_UnTrack(self);
  // THPVariable_clear(self);
  self->cdata.~_ListNestedTensor();
  Py_TYPE(self)->tp_free((PyObject*)self);
}

static PyObject* _ListNestedTensorVariable_pynew(
    PyTypeObject* type,
    PyObject* args,
    PyObject* kwargs) {
  PyObject* listObj;
  if (!PyArg_ParseTuple(args, "O!", &PyList_Type, &listObj)) {
    throw std::runtime_error("invalid arguments");
  }
  if (false) {
    if (PyObject_Length(listObj) > 0) {
      Variable first_variable = _get_first_variable(listObj);
      if (!_verify_variables(first_variable, listObj)) {
        throw std::runtime_error("Invalid list of Tensors");
      }
    }
  }
  return _ListNestedTensorVariable_NewWithVar(
      type, _ListNestedTensor(_get_structure(listObj)));
}

static PyObject* _ListNestedTensorVariable_requires_grad_(
    PyObject* self_,
    PyObject* bool_arg) {
  if (!PyBool_Check(bool_arg)) {
    throw std::runtime_error("Argument must be bool.");
  }
  auto& self = reinterpret_cast<_ListNestedTensorVariable*>(self_)->cdata;
  return _ListNestedTensorVariable_Wrap(
      self.requires_grad_(PyObject_IsTrue(bool_arg)));
}

static void _ListNestedTensorVariable_backward(
    PyObject* self_,
    PyObject* args) {
  PyObject* gradient_;
  PyObject* retain_graph_;
  PyObject* create_graph_;
  if (!PyArg_ParseTuple(
          args, "OOO", &gradient_, &retain_graph_, &create_graph_)) {
    throw std::runtime_error("tuple parsing failed");
  }
  if (!_ListNestedTensorVariable_Check(gradient_)) {
    throw std::runtime_error("variable parsing failed");
  }
  if (!PyBool_Check(retain_graph_)) {
    throw std::runtime_error("retain bool parsing failed");
  }
  if (!PyBool_Check(create_graph_)) {
    throw std::runtime_error("create graph bool parsing failed");
  }
  auto& self = reinterpret_cast<_ListNestedTensorVariable*>(self_)->cdata;
  auto& gradient =
      reinterpret_cast<_ListNestedTensorVariable*>(gradient_)->cdata;
  bool retain_graph = PyObject_IsTrue(retain_graph_);
  bool create_graph = PyObject_IsTrue(create_graph_);
  self.backward(gradient, retain_graph, create_graph);
}

static Py_ssize_t _ListNestedTensorVariable_len(PyObject* self_) {
  auto& self = reinterpret_cast<_ListNestedTensorVariable*>(self_)->cdata;
  return PyLong_AsSsize_t(PyLong_FromLong(self.__len__()));
}

static PyObject* _ListNestedTensorVariable_dtype(
    _ListNestedTensorVariable* self,
    void* unused) {
  HANDLE_TH_ERRORS
  auto& self_ = self->cdata;
  return wrap(torch::getDtype(self_.scalar_type()));
  END_HANDLE_TH_ERRORS
}

static PyObject* _ListNestedTensorVariable_layout(
    _ListNestedTensorVariable* self,
    void* unused) {
  HANDLE_TH_ERRORS
  auto& self_ = self->cdata;
  return wrap(torch::getLayout(self_.backend()));
  END_HANDLE_TH_ERRORS
}

static PyObject* _ListNestedTensorVariable_device(
    _ListNestedTensorVariable* self,
    void* unused) {
  HANDLE_TH_ERRORS
  auto& self_ = self->cdata;
  return THPDevice_New(self_.device());
  END_HANDLE_TH_ERRORS
}

static std::string _NestedNode___str__(const _NestedNode& nested_node) {
  std::stringstream result;
  if (nested_node._children.size() == 0) {
    PyObject* objectsRepresentation =
        PyObject_Str(THPVariable_Wrap(nested_node._variable_node._variable));
    result << THPUtils_unpackString(objectsRepresentation);
    return result.str();
  } else {
    result << "nested_tensor([";
    result << std::endl;
    for (_NestedNode node : nested_node._children) {
      result << "  ";
      result << _NestedNode___str__(node);
      result << ",";
      result << std::endl;
    }
    result << "])";
    return result.str();
  }
}

std::string _ListNestedTensor::__str__() {
  return _NestedNode___str__(_structure);
}

// NOTE: Don't delete this. repr is an important concept, this
// implementation is just faulty due to torch.Tensor.__repr__
// TODO: Assuming that there is no difference in __str__ and __repr__ for
// torch.Tensor.
std::string _ListNestedTensor::__repr__() {
  return _NestedNode___str__(_structure);
}

static struct PyGetSetDef _ListNestedTensorVariable_properties[] = {
    {"dtype",
     (getter)_ListNestedTensorVariable_dtype,
     nullptr,
     nullptr,
     nullptr},
    {"layout",
     (getter)_ListNestedTensorVariable_layout,
     nullptr,
     nullptr,
     nullptr},
    {"device",
     (getter)_ListNestedTensorVariable_device,
     nullptr,
     nullptr,
     nullptr},
    {"grad", (getter)_ListNestedTensorVariable_grad, nullptr, nullptr, nullptr},
    {"requires_grad",
     (getter)_ListNestedTensorVariable_requires_grad,
     nullptr,
     nullptr,
     nullptr},
    {nullptr}};

static PyMethodDef _ListNestedTensorVariable_methods[] = {
    {"element_size",
     (PyCFunction)_ListNestedTensorVariable_element_size,
     METH_NOARGS,
     "Return element size."},
    {"nested_dim",
     (PyCFunction)_ListNestedTensorVariable_nested_dim,
     METH_NOARGS,
     "Return nested dim."},
    {"nested_size",
     (PyCFunction)_ListNestedTensorVariable_nested_size,
     METH_NOARGS,
     "Return nested_size."},
    {"nested_stride",
     (PyCFunction)_ListNestedTensorVariable_nested_stride,
     METH_NOARGS,
     "Return nested_stride."},
    {"pin_memory",
     (PyCFunction)_ListNestedTensorVariable_pin_memory,
     METH_NOARGS,
     "Pins memory."},
    {"detach",
     (PyCFunction)_ListNestedTensorVariable_detach,
     METH_NOARGS,
     "Detaches and returns."},
    {"requires_grad_",
     (PyCFunction)_ListNestedTensorVariable_requires_grad_,
     METH_O,
     "requires_grad_ and returns."},
    {"backward",
     (PyCFunction)_ListNestedTensorVariable_backward,
     METH_VARARGS,
     "backward and returns."},
    {"is_pinned",
     (PyCFunction)_ListNestedTensorVariable_is_pinned,
     METH_NOARGS,
     "Returns is_pinned."},
    {"is_contiguous",
     (PyCFunction)_ListNestedTensorVariable_is_contiguous,
     METH_NOARGS,
     "Returns is_contiguous."},
    {"numel",
     (PyCFunction)_ListNestedTensorVariable_numel,
     METH_NOARGS,
     "Returns numel."},
    {"to_tensor",
     (PyCFunction)_ListNestedTensorVariable_to_tensor,
     METH_NOARGS,
     "Returns to_tensor."},
    {"dim",
     (PyCFunction)_ListNestedTensorVariable_dim,
     METH_NOARGS,
     "Returns dim."},
    {"to",
     (PyCFunction)_ListNestedTensorVariable_to,
     METH_VARARGS | METH_KEYWORDS,
     "Returns to."},
    {"unbind",
     (PyCFunction)_ListNestedTensorVariable_unbind,
     METH_NOARGS,
     "Returns unbound components."},
    {NULL} /* Sentinel */
};

static PySequenceMethods _ListNestedTensorVariable_as_sequence = {
    (lenfunc)_ListNestedTensorVariable_len, /* sq_length */
    nullptr, /* sq_concat */
    nullptr, /* sq_repeat */
    nullptr, /* sq_item */
    nullptr, /* sq_slice */
    nullptr, /* sq_ass_item */
    nullptr, /* sq_ass_slice */
    nullptr /* sq_contains */
};

// TODO: "Py_TPFLAGS_DEFAULT enables all memebers defined until Python 3.3"
// https://docs.python.org/3/extending/newtypes_tutorial.html
// Does that mean it won't work before Python 3.3?
PyTypeObject _ListNestedTensorVariableType = {
    PyVarObject_HEAD_INIT(nullptr, 0) "torch._ListNestedTensor", /* tp_name */
    sizeof(_ListNestedTensorVariable), /* tp_basicsize */
    0, /* tp_itemsize */
    (destructor)_ListNestedTensorVariable_dealloc, /* tp_dealloc */
    nullptr, /* tp_print */
    nullptr, /* tp_getattr */
    nullptr, /* tp_setattr */
    nullptr, /* tp_reserved */
    (reprfunc)_ListNestedTensorVariable___repr__, /* tp_repr */
    nullptr, /* tp_as_number */
    &_ListNestedTensorVariable_as_sequence, /* tp_as_sequence */
    nullptr, /* tp_as_mapping */
    nullptr, /* tp_hash  */
    nullptr, /* tp_call */
    (reprfunc)_ListNestedTensorVariable___str__, /* tp_str */
    nullptr, /* tp_getattro */
    nullptr, /* tp_setattro */
    nullptr, /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT, /* tp_flags */
    nullptr, /* tp_doc */
    nullptr, /* tp_traverse */
    nullptr, /* tp_clear */
    nullptr, /* tp_richcompare */
    0, /* tp_weaklistoffset */
    nullptr, /* tp_iter */
    nullptr, /* tp_iternext */
    _ListNestedTensorVariable_methods, /* tp_methods */
    nullptr, /* tp_members */
    _ListNestedTensorVariable_properties, /* tp_getset */
    nullptr, /* tp_base */
    nullptr, /* tp_dict */
    nullptr, /* tp_descr_get */
    nullptr, /* tp_descr_set */
    0, /* tp_dictoffset */
    nullptr, /* tp_init */
    nullptr, /* tp_alloc */
    _ListNestedTensorVariable_pynew, /* tp_new */
};

void initialize_python_bindings() {
  PyObject* m;
  if (PyType_Ready(&_ListNestedTensorVariableType) < 0) {
    throw python_error();
  }

  m = PyImport_ImportModule("torch");
  if (!m) {
    throw python_error();
  }

  Py_INCREF(&_ListNestedTensorVariableType);
  if (PyModule_AddObject(
          m, "_ListNestedTensor", (PyObject*)&_ListNestedTensorVariableType) <
      0) {
    Py_DECREF(&_ListNestedTensorVariableType);
    Py_DECREF(m);
    throw python_error();
  }
}
} // namespace nested_tensor
} // namespace torch
