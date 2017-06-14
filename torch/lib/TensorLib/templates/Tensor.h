#pragma once

#include "TensorLib/Scalar.h"
#include "TensorLib/Type.h"
#include "TensorLib/TensorImpl.h"
#include "TensorLib/Utils.h"

namespace tlib {
class Type;

struct Tensor {

  Tensor()
  : pImpl(nullptr){}
  Tensor(TensorImpl * self, bool retain = true)
  : pImpl(self) {
    if(pImpl != nullptr && retain)
      pImpl->retain();
  }
  Tensor(Tensor const & rhs)
  : pImpl(rhs.pImpl) {
    if(pImpl != nullptr)
      pImpl->retain();
  }
  Tensor(Tensor && rhs)
  : pImpl(rhs.pImpl) {
    rhs.pImpl = nullptr;
  }
  ~Tensor() {
    if(pImpl != nullptr)
      pImpl->release();
  }
  Tensor & operator=(Tensor && rhs) {
    rhs.swap(*this);
    return *this;
  }
  Tensor & operator=(Tensor const & rhs) {
      //Tensor ctor retains original rhs.pImpl
      //then rhs.pImpl is swapped with this->pImpl
      //finally Tensor dtor releases rhs.pImpl, which was originally this->pImpl
      Tensor(rhs).swap(*this);
      return *this;
  }
  void reset() {
    Tensor().swap(*this);
  }
  void reset(TensorImpl * rhs) {
    Tensor(rhs).swap(*this);
  }
  void reset(TensorImpl * rhs, bool retain) {
    Tensor(rhs, retain).swap(*this );
  }
  TensorImpl * get() {
    return pImpl;
  }
  TensorImpl * detach() {
    TensorImpl * ret = pImpl;
    pImpl = nullptr;
    return ret;
  }
  operator bool() const {
    return pImpl != nullptr;
  }
  void swap(Tensor & rhs) {
    TensorImpl * tmp = pImpl;
    pImpl = rhs.pImpl;
    rhs.pImpl = tmp;
  }
  const char * toString() const {
    return pImpl->toString();
  }
  IntList sizes() const {
    return pImpl->sizes();
  }
  IntList strides() const {
    return pImpl->strides();
  }
  Type & type() const {
    return pImpl->type();
  }
  Tensor toType(Type & t) const {
    if(type().ID() ==t.ID())
      return *this;
    return t.copy(*this);
  }
  Tensor toType(ScalarType t) {
    return toType(type().toScalarType(t));
  }
  Tensor toBackend(Backend b) {
    return toType(type().toBackend(b));
  }
  template<typename T>
  T * data() const;

  //example
  //Tensor * add(Tensor & b);
  ${tensor_method_declarations}

  friend class Type;

//TODO(zach): sort out friend classes
public:
  TensorImpl * pImpl;
};

// all static inline to allow for inlining of the non-dynamic part of dispatch
${tensor_method_definitions}

template<typename T>
inline T* Tensor::data() const {
  runtime_error("data() cast to unexpected type.");
}
#define DEFINE_CAST(T,name,_) \
template<> \
inline T* Tensor::data() const { \
  TLIB_ASSERT(type().scalarType() == ScalarType::name, \
    "expected scalar type % s but found %s", #name, \
    tlib::toString(type().scalarType())); \
  return static_cast<T*>(this->data_ptr()); \
}

TLIB_FORALL_SCALAR_TYPES(DEFINE_CAST)
#undef DEFINE_CAST

} //namespace tlib
