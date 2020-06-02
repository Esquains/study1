#include <torch/csrc/utils/disable_torch_function.h>

namespace torch {
  static thread_local bool enable_torch_function = true;

  inline bool torch_function_enabled() {
      return enable_torch_function;
  }
}

typedef struct {
    PyObject_HEAD
    /* Type-specific fields go here. */
    bool old_state;
} DisableTorchFunction;

PyObject* DisableTorchFunction__enter(PyObject* self, PyObject *unused) {
    ((DisableTorchFunction*)self)->old_state = torch::enable_torch_function;
    torch::enable_torch_function = false;
    Py_RETURN_NONE;
}

PyObject* DisableTorchFunction__exit(PyObject* self, PyObject *unused) {
    torch::enable_torch_function = ((DisableTorchFunction*)self)->old_state;
    Py_RETURN_NONE;
}

PyObject* THPModule_isEnabledTorchFunction(PyObject* self, PyObject *unused) {
    if (torch::enable_torch_function) {
        Py_RETURN_TRUE;
    } else
    {
        Py_RETURN_FALSE;
    }
}

static PyMethodDef DisableTorchFunction_methods[] = {
  {"__enter__",  (PyCFunction)DisableTorchFunction__enter, METH_NOARGS,  nullptr},
  {"__exit__",   (PyCFunction)DisableTorchFunction__exit,  METH_VARARGS, nullptr},
  {nullptr, nullptr, 0, nullptr}
};

PyTypeObject DisableTorchFunctionType = {
  PyVarObject_HEAD_INIT(nullptr, 0)
  "torch._C.DisableTorchFunction",             /* tp_name */
  sizeof(DisableTorchFunction),                /* tp_basicsize */
  0,                                           /* tp_itemsize */
  nullptr,                                     /* tp_dealloc */
  0,                                           /* tp_vectorcall_offset */
  nullptr,                                     /* tp_getattr */
  nullptr,                                     /* tp_setattr */
  nullptr,                                     /* tp_reserved */
  nullptr,                                     /* tp_repr */
  nullptr,                                     /* tp_as_number */
  nullptr,                                     /* tp_as_sequence */
  nullptr,                                     /* tp_as_mapping */
  nullptr,                                     /* tp_hash  */
  nullptr,                                     /* tp_call */
  nullptr,                                     /* tp_str */
  nullptr,                                     /* tp_getattro */
  nullptr,                                     /* tp_setattro */
  nullptr,                                     /* tp_as_buffer */
  Py_TPFLAGS_DEFAULT,                          /* tp_flags */
  nullptr,                                     /* tp_doc */
  nullptr,                                     /* tp_traverse */
  nullptr,                                     /* tp_clear */
  nullptr,                                     /* tp_richcompare */
  0,                                           /* tp_weaklistoffset */
  nullptr,                                     /* tp_iter */
  nullptr,                                     /* tp_iternext */
  DisableTorchFunction_methods,                /* tp_methods */
  nullptr,                                     /* tp_members */
  nullptr,                                     /* tp_getset */
  nullptr,                                     /* tp_base */
  nullptr,                                     /* tp_dict */
  nullptr,                                     /* tp_descr_get */
  nullptr,                                     /* tp_descr_set */
  0,                                           /* tp_dictoffset */
  nullptr,                                     /* tp_init */
  PyType_GenericAlloc,                         /* tp_alloc */
  PyType_GenericNew,                           /* tp_new */
};

PyObject* THPModule_DisableTorchFunctionType() {
  if (PyType_Ready(&DisableTorchFunctionType) < 0) {
      return nullptr;
  }

  return (PyObject *)(&DisableTorchFunctionType);
}
