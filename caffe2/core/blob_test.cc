#include <iostream>
#include <memory>
#include <mutex>

#include "caffe2/core/blob.h"
#include "caffe2/core/common.h"
#include "caffe2/core/context.h"
#include "caffe2/core/db.h"
#include "caffe2/core/operator.h"
#include "caffe2/core/qtensor.h"
#include "caffe2/core/qtensor_serialization.h"
#include "caffe2/core/tensor.h"
#include "caffe2/core/types.h"
#include "caffe2/core/workspace.h"
#include "caffe2/proto/caffe2.pb.h"
#include "caffe2/utils/proto_utils.h"
#include <gtest/gtest.h>

CAFFE2_DEFINE_int64(caffe2_test_big_tensor_size, 100000000, "");
CAFFE2_DECLARE_int(caffe2_tensor_chunk_size);

namespace caffe2 {

using namespace ::caffe2::db;

namespace {

class BlobTestFoo {};
class BlobTestBar {};
}

CAFFE_KNOWN_TYPE(BlobTestFoo);
CAFFE_KNOWN_TYPE(BlobTestBar);

namespace {

TEST(BlobTest, Blob) {
  Blob blob;

  int* int_unused UNUSED_VARIABLE = blob.GetMutable<int>();
  EXPECT_TRUE(blob.IsType<int>());
  EXPECT_FALSE(blob.IsType<BlobTestFoo>());

  BlobTestFoo* foo_unused UNUSED_VARIABLE = blob.GetMutable<BlobTestFoo>();
  EXPECT_TRUE(blob.IsType<BlobTestFoo>());
  EXPECT_FALSE(blob.IsType<int>());
}

TEST(BlobTest, BlobNewObjectFlag) {
  Blob blob;

  bool is_new_object = true;

  blob.GetMutable<int>(&is_new_object);
  EXPECT_TRUE(is_new_object);
  blob.GetMutable<int>(&is_new_object);
  EXPECT_FALSE(is_new_object);

  blob.GetMutable<BlobTestFoo>(&is_new_object);
  EXPECT_TRUE(is_new_object);
  blob.GetMutable<BlobTestFoo>(&is_new_object);
  EXPECT_FALSE(is_new_object);
}

TEST(BlobTest, BlobUninitialized) {
  Blob blob;
  ASSERT_THROW(blob.Get<int>(), EnforceNotMet);
}

TEST(BlobTest, BlobWrongType) {
  Blob blob;
  BlobTestFoo* foo_unused UNUSED_VARIABLE = blob.GetMutable<BlobTestFoo>();
  EXPECT_TRUE(blob.IsType<BlobTestFoo>());
  EXPECT_FALSE(blob.IsType<int>());
  // When not null, we should only call with the right type.
  EXPECT_NE(&blob.Get<BlobTestFoo>(), nullptr);
  ASSERT_THROW(blob.Get<int>(), EnforceNotMet);
}

TEST(BlobTest, BlobReset) {
  Blob blob;
  std::unique_ptr<BlobTestFoo> foo(new BlobTestFoo());
  EXPECT_TRUE(blob.Reset(foo.release()) != nullptr);
  // Also test that Reset works.
  blob.Reset();
}

TEST(BlobTest, BlobMove) {
  Blob blob1;
  std::unique_ptr<BlobTestFoo> foo(new BlobTestFoo());
  auto* fooPtr = foo.get();
  EXPECT_TRUE(blob1.Reset(foo.release()) != nullptr);
  Blob blob2;
  blob2 = std::move(blob1);
  ASSERT_THROW(blob1.Get<BlobTestFoo>(), EnforceNotMet);
  EXPECT_EQ(&blob2.Get<BlobTestFoo>(), fooPtr);
  Blob blob3{std::move(blob2)};
  EXPECT_EQ(&blob3.Get<BlobTestFoo>(), fooPtr);
}

TEST(BlobTest, BlobShareExternalPointer) {
  Blob blob;
  std::unique_ptr<BlobTestFoo> foo(new BlobTestFoo());
  EXPECT_EQ(blob.ShareExternal<BlobTestFoo>(foo.get()), foo.get());
  EXPECT_TRUE(blob.IsType<BlobTestFoo>());
  // Also test that Reset works.
  blob.Reset();
}

TEST(BlobTest, BlobShareExternalObject) {
  Blob blob;
  BlobTestFoo foo;
  EXPECT_EQ(blob.ShareExternal<BlobTestFoo>(&foo), &foo);
  EXPECT_TRUE(blob.IsType<BlobTestFoo>());
  // Also test that Reset works.
  blob.Reset();
}

TEST(BlobTest, StringSerialization) {
  const std::string kTestString = "Hello world?";
  Blob blob;
  *blob.GetMutable<std::string>() = kTestString;

  string serialized = blob.Serialize("test");
  BlobProto proto;
  CHECK(proto.ParseFromString(serialized));
  EXPECT_EQ(proto.name(), "test");
  EXPECT_EQ(proto.type(), "std::string");
  EXPECT_FALSE(proto.has_tensor());
  EXPECT_EQ(proto.content(), kTestString);
}

TEST(TensorNonTypedTest, TensorChangeType) {
  vector<int> dims(3);
  dims[0] = 2;
  dims[1] = 3;
  dims[2] = 5;
  TensorCPU tensor(dims);
  EXPECT_TRUE(tensor.mutable_data<int>() != nullptr);
  EXPECT_TRUE(tensor.data<int>() != nullptr);
  EXPECT_TRUE(tensor.meta().Match<int>());

  EXPECT_TRUE(tensor.mutable_data<float>() != nullptr);
  EXPECT_TRUE(tensor.data<float>() != nullptr);
  EXPECT_TRUE(tensor.meta().Match<float>());
}

template <typename T> class TensorCPUTest : public ::testing::Test {};
template <typename T> class TensorCPUDeathTest : public ::testing::Test {};
typedef ::testing::Types<char, int, float> TensorTypes;
TYPED_TEST_CASE(TensorCPUTest, TensorTypes);
TYPED_TEST_CASE(TensorCPUDeathTest, TensorTypes);

TYPED_TEST(TensorCPUTest, TensorInitializedEmpty) {
  TensorCPU tensor;
  EXPECT_EQ(tensor.ndim(), 0);
  vector<int> dims(3);
  dims[0] = 2;
  dims[1] = 3;
  dims[2] = 5;
  tensor.Resize(dims);
  EXPECT_EQ(tensor.ndim(), 3);
  EXPECT_EQ(tensor.dim32(0), 2);
  EXPECT_EQ(tensor.dim32(1), 3);
  EXPECT_EQ(tensor.dim32(2), 5);
  EXPECT_EQ(tensor.size(), 2 * 3 * 5);
  EXPECT_TRUE(tensor.mutable_data<TypeParam>() != nullptr);
  EXPECT_TRUE(tensor.data<TypeParam>() != nullptr);
}

TYPED_TEST(TensorCPUTest, TensorInitializedNonEmpty) {
  vector<int> dims(3);
  dims[0] = 2;
  dims[1] = 3;
  dims[2] = 5;
  TensorCPU tensor(dims);
  EXPECT_EQ(tensor.ndim(), 3);
  EXPECT_EQ(tensor.dim32(0), 2);
  EXPECT_EQ(tensor.dim32(1), 3);
  EXPECT_EQ(tensor.dim32(2), 5);
  EXPECT_TRUE(tensor.mutable_data<TypeParam>() != nullptr);
  EXPECT_TRUE(tensor.data<TypeParam>() != nullptr);
  dims[0] = 7;
  dims[1] = 11;
  dims[2] = 13;
  dims.push_back(17);
  tensor.Resize(dims);
  EXPECT_EQ(tensor.ndim(), 4);
  EXPECT_EQ(tensor.dim32(0), 7);
  EXPECT_EQ(tensor.dim32(1), 11);
  EXPECT_EQ(tensor.dim32(2), 13);
  EXPECT_EQ(tensor.dim32(3), 17);
  EXPECT_TRUE(tensor.mutable_data<TypeParam>() != nullptr);
  EXPECT_TRUE(tensor.data<TypeParam>() != nullptr);
}

TYPED_TEST(TensorCPUTest, TensorInitializedZeroDim) {
  vector<int> dims(3);
  dims[0] = 2;
  dims[1] = 0;
  dims[2] = 5;
  TensorCPU tensor(dims);
  EXPECT_EQ(tensor.ndim(), 3);
  EXPECT_EQ(tensor.dim32(0), 2);
  EXPECT_EQ(tensor.dim32(1), 0);
  EXPECT_EQ(tensor.dim32(2), 5);
  EXPECT_TRUE(tensor.mutable_data<TypeParam>() == nullptr);
  EXPECT_TRUE(tensor.data<TypeParam>() == nullptr);
}

TYPED_TEST(TensorCPUTest, TensorResizeZeroDim) {
  vector<int> dims(3);
  dims[0] = 2;
  dims[1] = 3;
  dims[2] = 5;
  TensorCPU tensor(dims);
  EXPECT_EQ(tensor.ndim(), 3);
  EXPECT_EQ(tensor.dim32(0), 2);
  EXPECT_EQ(tensor.dim32(1), 3);
  EXPECT_EQ(tensor.dim32(2), 5);
  EXPECT_TRUE(tensor.mutable_data<TypeParam>() != nullptr);
  EXPECT_TRUE(tensor.data<TypeParam>() != nullptr);

  dims[0] = 7;
  dims[1] = 0;
  dims[2] = 13;
  tensor.Resize(dims);
  EXPECT_EQ(tensor.size(), 0);
  EXPECT_EQ(tensor.ndim(), 3);
  EXPECT_EQ(tensor.dim32(0), 7);
  EXPECT_EQ(tensor.dim32(1), 0);
  EXPECT_EQ(tensor.dim32(2), 13);
  // output value can be arbitrary, but the call to data() shouldn't crash
  tensor.mutable_data<TypeParam>();
  tensor.data<TypeParam>();
}

TYPED_TEST(TensorCPUTest, TensorInitializedScalar) {
  vector<int> dims;
  TensorCPU tensor(dims);
  EXPECT_EQ(tensor.ndim(), 0);
  EXPECT_EQ(tensor.size(), 1);
  EXPECT_TRUE(tensor.mutable_data<TypeParam>() != nullptr);
  EXPECT_TRUE(tensor.data<TypeParam>() != nullptr);
}

TYPED_TEST(TensorCPUTest, TensorShareData) {
  vector<int> dims(3);
  dims[0] = 2;
  dims[1] = 3;
  dims[2] = 5;
  TensorCPU tensor(dims);
  TensorCPU other_tensor(dims);
  EXPECT_TRUE(tensor.mutable_data<TypeParam>() != nullptr);
  other_tensor.ShareData(tensor);
  EXPECT_TRUE(tensor.data<TypeParam>() != nullptr);
  EXPECT_TRUE(other_tensor.data<TypeParam>() != nullptr);
  EXPECT_EQ(tensor.data<TypeParam>(), other_tensor.data<TypeParam>());
  // Set one value, check the other
  for (int i = 0; i < tensor.size(); ++i) {
    tensor.mutable_data<TypeParam>()[i] = i;
    EXPECT_EQ(other_tensor.data<TypeParam>()[i], i);
  }
}

TYPED_TEST(TensorCPUTest, TensorShareDataRawPointer) {
  vector<int> dims(3);
  dims[0] = 2;
  dims[1] = 3;
  dims[2] = 5;
  std::unique_ptr<TypeParam[]> raw_buffer(new TypeParam[2*3*5]);
  TensorCPU tensor(dims);
  tensor.ShareExternalPointer(raw_buffer.get());
  EXPECT_EQ(tensor.mutable_data<TypeParam>(), raw_buffer.get());
  EXPECT_EQ(tensor.data<TypeParam>(), raw_buffer.get());
  // Set one value, check the other
  for (int i = 0; i < tensor.size(); ++i) {
    raw_buffer.get()[i] = i;
    EXPECT_EQ(tensor.data<TypeParam>()[i], i);
  }
}

TYPED_TEST(TensorCPUTest, TensorShareDataRawPointerWithMeta) {
  vector<int> dims(3);
  dims[0] = 2;
  dims[1] = 3;
  dims[2] = 5;
  std::unique_ptr<TypeParam[]> raw_buffer(new TypeParam[2 * 3 * 5]);
  TensorCPU tensor(dims);
  TypeMeta meta = TypeMeta::Make<TypeParam>();
  tensor.ShareExternalPointer(raw_buffer.get(), meta);
  EXPECT_EQ(tensor.mutable_data<TypeParam>(), raw_buffer.get());
  EXPECT_EQ(tensor.data<TypeParam>(), raw_buffer.get());
  // Set one value, check the other
  for (int i = 0; i < tensor.size(); ++i) {
    raw_buffer.get()[i] = i;
    EXPECT_EQ(tensor.data<TypeParam>()[i], i);
  }
}

TYPED_TEST(TensorCPUTest, CannotShareDataWhenShapeNotSet) {
  std::unique_ptr<TypeParam[]> raw_buffer(new TypeParam[10]);
  TensorCPU tensor;
  ASSERT_THROW(tensor.ShareExternalPointer(raw_buffer.get()), EnforceNotMet);
}

TYPED_TEST(TensorCPUTest, TensorShareDataCanUseDifferentShapes) {
  vector<int> dims(3);
  dims[0] = 2;
  dims[1] = 3;
  dims[2] = 5;
  vector<int> alternate_dims(1);
  alternate_dims[0] = 2 * 3 * 5;
  TensorCPU tensor(dims);
  TensorCPU other_tensor(alternate_dims);
  EXPECT_TRUE(tensor.mutable_data<TypeParam>() != nullptr);
  other_tensor.ShareData(tensor);
  EXPECT_EQ(other_tensor.ndim(), 1);
  EXPECT_EQ(other_tensor.dim32(0), alternate_dims[0]);
  EXPECT_TRUE(tensor.data<TypeParam>() != nullptr);
  EXPECT_TRUE(other_tensor.data<TypeParam>() != nullptr);
  EXPECT_EQ(tensor.data<TypeParam>(), other_tensor.data<TypeParam>());
  // Set one value, check the other
  for (int i = 0; i < tensor.size(); ++i) {
    tensor.mutable_data<TypeParam>()[i] = i;
    EXPECT_EQ(other_tensor.data<TypeParam>()[i], i);
  }
}


TYPED_TEST(TensorCPUTest, NoLongerSharesAfterResize) {
  vector<int> dims(3);
  dims[0] = 2;
  dims[1] = 3;
  dims[2] = 5;
  TensorCPU tensor(dims);
  TensorCPU other_tensor(dims);
  EXPECT_TRUE(tensor.mutable_data<TypeParam>() != nullptr);
  other_tensor.ShareData(tensor);
  EXPECT_EQ(tensor.data<TypeParam>(), other_tensor.data<TypeParam>());
  auto* old_pointer = other_tensor.data<TypeParam>();

  dims[0] = 7;
  tensor.Resize(dims);
  EXPECT_EQ(old_pointer, other_tensor.data<TypeParam>());
  EXPECT_NE(old_pointer, tensor.mutable_data<TypeParam>());
}

TYPED_TEST(TensorCPUTest, KeepOnShrink) {
  FLAGS_caffe2_keep_on_shrink = true;
  FLAGS_caffe2_max_keep_on_shrink_memory = LLONG_MAX;
  vector<int> dims{2, 3, 5};
  TensorCPU tensor(dims);
  TypeParam* ptr = tensor.mutable_data<TypeParam>();
  EXPECT_TRUE(ptr != nullptr);
  // Expanding - will reallocate
  tensor.Resize(3, 4, 6);
  TypeParam* larger_ptr = tensor.mutable_data<TypeParam>();
  EXPECT_TRUE(larger_ptr != nullptr);
  EXPECT_NE(ptr, larger_ptr);
  // Shrinking - will not reallocate
  tensor.Resize(1, 2, 4);
  TypeParam* smaller_ptr = tensor.mutable_data<TypeParam>();
  EXPECT_TRUE(smaller_ptr != nullptr);
  EXPECT_EQ(larger_ptr, smaller_ptr);
  // resize to 0 in the meantime;
  tensor.Resize(3, 0, 6);
  // Expanding but still under capacity - will not reallocate
  tensor.Resize(2, 3, 5);
  TypeParam* new_ptr = tensor.mutable_data<TypeParam>();
  EXPECT_TRUE(new_ptr != nullptr);
  EXPECT_EQ(larger_ptr, new_ptr);
}

TYPED_TEST(TensorCPUTest, MaxKeepOnShrink) {
  FLAGS_caffe2_keep_on_shrink = true;
  // Remember that this tests for int, float and char
  FLAGS_caffe2_max_keep_on_shrink_memory = 40;
  vector<int> dims{1, 8, 8};
  TensorCPU tensor(dims);
  TypeParam* ptr = tensor.mutable_data<TypeParam>();
  EXPECT_TRUE(ptr != nullptr);
  // Shrinking - will not reallocate
  tensor.Resize(1, 7, 8);
  TypeParam* smaller_ptr = tensor.mutable_data<TypeParam>();
  EXPECT_TRUE(smaller_ptr != nullptr);
  EXPECT_EQ(ptr, smaller_ptr);
  // Resize to more than maximum shrink, should reallocate
  tensor.Resize(1, 1, 8);
  TypeParam* new_ptr = tensor.mutable_data<TypeParam>();
  EXPECT_TRUE(new_ptr != nullptr);
  EXPECT_NE(ptr, new_ptr);
}

TYPED_TEST(TensorCPUDeathTest, CannotAccessRawDataWhenEmpty) {
  TensorCPU tensor;
  EXPECT_EQ(tensor.ndim(), 0);
  ASSERT_ANY_THROW(tensor.raw_data());
}

TYPED_TEST(TensorCPUDeathTest, CannotAccessDataWhenEmpty) {
  TensorCPU tensor;
  EXPECT_EQ(tensor.ndim(), 0);
  ASSERT_ANY_THROW(tensor.data<TypeParam>());
}

TEST(TensorTest, TensorNonFundamentalType) {
  TensorCPU tensor(vector<int>{2, 3, 4});
  EXPECT_TRUE(tensor.mutable_data<std::string>() != nullptr);
  const std::string* ptr = tensor.data<std::string>();
  for (int i = 0; i < tensor.size(); ++i) {
    EXPECT_TRUE(ptr[i] == "");
  }
}

TEST(TensorTest, TensorNonFundamentalTypeCopy) {
  TensorCPU tensor(vector<int>{2, 3, 4});
  std::string* ptr = tensor.mutable_data<std::string>();
  EXPECT_TRUE(ptr != nullptr);
  for (int i = 0; i < tensor.size(); ++i) {
    EXPECT_TRUE(ptr[i] == "");
    ptr[i] = "filled";
  }
  TensorCPU dst_tensor(tensor);
  const std::string* dst_ptr = dst_tensor.data<std::string>();
  for (int i = 0; i < dst_tensor.size(); ++i) {
    EXPECT_TRUE(dst_ptr[i] == "filled");
  }
}

TEST(TensorTest, Tensor64BitDimension) {
  // Initialize a large tensor.
  TIndex large_number =
      static_cast<int64_t>(std::numeric_limits<int>::max()) + 1;
  TensorCPU tensor(vector<TIndex>{large_number});
  EXPECT_EQ(tensor.ndim(), 1);
  EXPECT_EQ(tensor.dim(0), large_number);
  EXPECT_EQ(tensor.size(), large_number);
  EXPECT_TRUE(tensor.mutable_data<char>() != nullptr);
  EXPECT_EQ(tensor.nbytes(), large_number * sizeof(char));
  EXPECT_EQ(tensor.itemsize(), sizeof(char));
  // Try to go even larger, but this time we will not do mutable_data because we
  // do not have a large enough memory.
  tensor.Resize(large_number, 100);
  EXPECT_EQ(tensor.ndim(), 2);
  EXPECT_EQ(tensor.dim(0), large_number);
  EXPECT_EQ(tensor.dim(1), 100);
  EXPECT_EQ(tensor.size(), large_number * 100);
}

TEST(TensorDeathTest, CannotCastDownLargeDims) {
  TIndex large_number =
      static_cast<int64_t>(std::numeric_limits<int>::max()) + 1;
  TensorCPU tensor(vector<TIndex>{large_number});
  EXPECT_EQ(tensor.ndim(), 1);
  EXPECT_EQ(tensor.dim(0), large_number);
  ASSERT_THROW(tensor.dim32(0), EnforceNotMet);
}

#define TEST_SERIALIZATION_WITH_TYPE(TypeParam, field_name)               \
  TEST(TensorTest, TensorSerialization_##TypeParam) {                     \
    Blob blob;                                                            \
    TensorCPU* tensor = blob.GetMutable<TensorCPU>();                     \
    tensor->Resize(2, 3);                                                 \
    for (int i = 0; i < 6; ++i) {                                         \
      tensor->mutable_data<TypeParam>()[i] = static_cast<TypeParam>(i);   \
    }                                                                     \
    string serialized = blob.Serialize("test");                           \
    BlobProto proto;                                                      \
    CHECK(proto.ParseFromString(serialized));                             \
    EXPECT_EQ(proto.name(), "test");                                      \
    EXPECT_EQ(proto.type(), "Tensor");                                    \
    EXPECT_TRUE(proto.has_tensor());                                      \
    const TensorProto& tensor_proto = proto.tensor();                     \
    EXPECT_EQ(                                                            \
        tensor_proto.data_type(),                                         \
        TypeMetaToDataType(TypeMeta::Make<TypeParam>()));                 \
    EXPECT_EQ(tensor_proto.field_name##_size(), 6);                       \
    for (int i = 0; i < 6; ++i) {                                         \
      EXPECT_EQ(tensor_proto.field_name(i), static_cast<TypeParam>(i));   \
    }                                                                     \
    Blob new_blob;                                                        \
    EXPECT_NO_THROW(new_blob.Deserialize(serialized));                    \
    EXPECT_TRUE(new_blob.IsType<TensorCPU>());                            \
    const TensorCPU& new_tensor = blob.Get<TensorCPU>();                  \
    EXPECT_EQ(new_tensor.ndim(), 2);                                      \
    EXPECT_EQ(new_tensor.dim(0), 2);                                      \
    EXPECT_EQ(new_tensor.dim(1), 3);                                      \
    for (int i = 0; i < 6; ++i) {                                         \
      EXPECT_EQ(                                                          \
          tensor->data<TypeParam>()[i], new_tensor.data<TypeParam>()[i]); \
    }                                                                     \
  }                                                                       \
                                                                          \
  TEST(EmptyTensorTest, TensorSerialization_##TypeParam) {                \
    Blob blob;                                                            \
    TensorCPU* tensor = blob.GetMutable<TensorCPU>();                     \
    tensor->Resize(0, 3);                                                 \
    tensor->mutable_data<TypeParam>();                                    \
    string serialized = blob.Serialize("test");                           \
    BlobProto proto;                                                      \
    CHECK(proto.ParseFromString(serialized));                             \
    EXPECT_EQ(proto.name(), "test");                                      \
    EXPECT_EQ(proto.type(), "Tensor");                                    \
    EXPECT_TRUE(proto.has_tensor());                                      \
    const TensorProto& tensor_proto = proto.tensor();                     \
    EXPECT_EQ(                                                            \
        tensor_proto.data_type(),                                         \
        TypeMetaToDataType(TypeMeta::Make<TypeParam>()));                 \
    EXPECT_EQ(tensor_proto.field_name##_size(), 0);                       \
    Blob new_blob;                                                        \
    EXPECT_NO_THROW(new_blob.Deserialize(serialized));                    \
    EXPECT_TRUE(new_blob.IsType<TensorCPU>());                            \
    const TensorCPU& new_tensor = blob.Get<TensorCPU>();                  \
    EXPECT_EQ(new_tensor.ndim(), 2);                                      \
    EXPECT_EQ(new_tensor.dim(0), 0);                                      \
    EXPECT_EQ(new_tensor.dim(1), 3);                                      \
  }

TEST_SERIALIZATION_WITH_TYPE(bool, int32_data)
TEST_SERIALIZATION_WITH_TYPE(double, double_data)
TEST_SERIALIZATION_WITH_TYPE(float, float_data)
TEST_SERIALIZATION_WITH_TYPE(int, int32_data)
TEST_SERIALIZATION_WITH_TYPE(int8_t, int32_data)
TEST_SERIALIZATION_WITH_TYPE(int16_t, int32_data)
TEST_SERIALIZATION_WITH_TYPE(uint8_t, int32_data)
TEST_SERIALIZATION_WITH_TYPE(uint16_t, int32_data)
TEST_SERIALIZATION_WITH_TYPE(int64_t, int64_data)

TEST(QTensorTest, QTensorSerialization) {
  Blob blob;
  QTensor<CPUContext>* qtensor = blob.GetMutable<QTensor<CPUContext>>();
  qtensor->SetPrecision(5);
  qtensor->SetSigned(false);
  qtensor->SetScale(1.337);
  qtensor->SetBias(-1.337);
  qtensor->Resize(std::vector<int>{2, 3});
  // "Randomly" set bits.
  srand(0);
  for (int i = 0; i < 6; ++i) {
    for (int j = 0; j < 5; ++j) {
      qtensor->SetBitAtIndex(j, i, rand() % 2);
    }
  }

  string serialized = blob.Serialize("test");
  BlobProto proto;
  CHECK(proto.ParseFromString(serialized));
  EXPECT_EQ(proto.name(), "test");
  EXPECT_EQ(proto.type(), "QTensor");
  EXPECT_TRUE(proto.has_qtensor());
  const QTensorProto& qtensor_proto = proto.qtensor();

  EXPECT_EQ(qtensor_proto.precision(), qtensor->precision());
  EXPECT_EQ(qtensor_proto.scale(), qtensor->scale());
  EXPECT_EQ(qtensor_proto.bias(), qtensor->bias());
  EXPECT_EQ(qtensor_proto.is_signed(), qtensor->is_signed());

  Blob new_blob;
  new_blob.Deserialize(serialized);
  EXPECT_TRUE(new_blob.IsType<QTensor<CPUContext>>());
  const QTensor<CPUContext>& new_qtensor = blob.Get<QTensor<CPUContext>>();
  EXPECT_EQ(new_qtensor.ndim(), 2);
  EXPECT_EQ(new_qtensor.dim32(0), 2);
  EXPECT_EQ(new_qtensor.dim32(1), 3);
  for (int i = 0; i < 6; ++i) {
    for (int j = 0; j < 5; ++j) {
      EXPECT_EQ(qtensor->GetBitAtIndex(j, i), new_qtensor.GetBitAtIndex(j, i));
    }
  }
}

typedef double my_type;

typedef std::vector<std::pair<string, string>> StringMap;

class VectorCursor : public db::Cursor {
 public:
  explicit VectorCursor(StringMap* data) : data_(data) {
    pos_ = 0;
  }
  ~VectorCursor() {}
  void Seek(const string& key) override {}
  void SeekToFirst() override {}
  void Next() override {
    ++pos_;
  }
  string key() override {
    return (*data_)[pos_].first;
  }
  string value() override {
    return (*data_)[pos_].second;
  }
  bool Valid() override {
    return pos_ < data_->size();
  }
 private:
  StringMap* data_ = nullptr;
  size_t pos_ = 0;
};

class VectorDB : public db::DB {
 public:
  VectorDB(const string& source, db::Mode mode)
      : DB(source, mode), name_(source) {}
  ~VectorDB() {
    data_.erase(name_);
  }
  void Close() override {}
  std::unique_ptr<db::Cursor> NewCursor() override {
    return make_unique<VectorCursor>(getData());
  }
  std::unique_ptr<db::Transaction> NewTransaction() override {
    CAFFE_THROW("Not implemented");
  }
  static void registerData(const string& name, StringMap&& data) {
    std::lock_guard<std::mutex> guard(dataRegistryMutex_);
    data_[name] = std::move(data);
  }
 private:
  StringMap* getData() {
    auto it = data_.find(name_);
    CAFFE_ENFORCE(it != data_.end(), "Can't find ", name_);
    return &(it->second);
  }
 private:
  string name_;
  static std::mutex dataRegistryMutex_;
  static std::map<string, StringMap> data_;
};

std::mutex VectorDB::dataRegistryMutex_;
std::map<string, StringMap> VectorDB::data_;

REGISTER_CAFFE2_DB(vector_db, VectorDB);

template <typename TypeParam>
class TypedTensorTest : public ::testing::Test {};
typedef ::testing::
    Types<float, bool, double, int, int8_t, int16_t, uint8_t, uint16_t, int64_t>
        TensorDataTypes;
TYPED_TEST_CASE(TypedTensorTest, TensorDataTypes);

TYPED_TEST(TypedTensorTest, BigTensorSerialization) {
  int64_t d1 = 2;
  int64_t d2 = FLAGS_caffe2_test_big_tensor_size
      ? FLAGS_caffe2_test_big_tensor_size / d1
      : static_cast<int64_t>(std::numeric_limits<int>::max()) + 1;
  int64_t size = d1 * d2;
  string db_source = (string)std::tmpnam(nullptr);
  LOG(INFO) << "db_source: " << db_source;

  {
    LOG(INFO) << "Test begin";
    Blob blob;
    TensorCPU* tensor = blob.GetMutable<TensorCPU>();
    LOG(INFO) << "Allocating blob";
    tensor->Resize(d1, d2);
    auto mutableData = tensor->mutable_data<TypeParam>();
    LOG(INFO) << "Filling out the blob";
    for (int64_t i = 0; i < size; ++i) {
      mutableData[i] = static_cast<TypeParam>(i);
    }
    StringMap data;
    std::mutex mutex;
    /*auto db = CreateDB("minidb", db_source, WRITE);*/
    auto acceptor = [&](const std::string& key, const std::string& value) {
      std::lock_guard<std::mutex> guard(mutex);
      /*db->NewTransaction()->Put(key, value);*/
      data.emplace_back(key, value);
    };
    blob.Serialize("test", acceptor);
    VectorDB::registerData(db_source, std::move(data));
    LOG(INFO) << "finished writing to DB";
  }

  {
    DeviceOption option;
    option.set_device_type(CPU);
    Argument db_type_arg = MakeArgument<string>("db_type", "vector_db");
    Argument absolute_path_arg = MakeArgument<bool>("absolute_path", true);
    Argument db_source_arg = MakeArgument<string>("db", db_source);
    auto op_def = CreateOperatorDef(
        "Load",
        "",
        std::vector<string>{},
        std::vector<string>({"test"}),
        std::vector<Argument>{db_type_arg, db_source_arg, absolute_path_arg},
        option,
        "DUMMY_ENGINE");
    Workspace ws;
    auto load_op = CreateOperator(op_def, &ws);
    EXPECT_TRUE(load_op != nullptr);
    LOG(INFO) << "Running operator";

    load_op->Run();
    LOG(INFO) << "Reading blob from workspace";
    auto new_blob = ws.GetBlob("test");
    EXPECT_TRUE(new_blob->IsType<TensorCPU>());
    const auto& new_tensor = new_blob->Get<TensorCPU>();

    EXPECT_EQ(new_tensor.ndim(), d1);
    EXPECT_EQ(new_tensor.dim(0), d1);
    EXPECT_EQ(new_tensor.dim(1), d2);
    for (int64_t i = 0; i < size; ++i) {
      EXPECT_EQ(static_cast<TypeParam>(i), new_tensor.data<TypeParam>()[i]);
    }
  }
}

TEST(CustomChunkSize, BigTensorSerialization) {
  int64_t d1 = 2;
  int64_t d2 = FLAGS_caffe2_test_big_tensor_size
      ? FLAGS_caffe2_test_big_tensor_size / d1
      : static_cast<int64_t>(std::numeric_limits<int>::max()) + 1;
  int64_t size = d1 * d2;

  Blob blob;
  TensorCPU* tensor = blob.GetMutable<TensorCPU>();
  tensor->Resize(d1, d2);
  tensor->mutable_data<float>();
  std::mutex mutex;
  int counter = 0;
  auto acceptor = [&](const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> guard(mutex);
    counter++;
  };
  blob.Serialize("test", acceptor, size);
  EXPECT_EQ(counter, 1);

  counter = 0;
  blob.Serialize("test", acceptor, (size / 2) + 1);
  EXPECT_EQ(counter, 2);

  counter = 0;
  blob.Serialize("test", acceptor, kNoChunking);
  EXPECT_EQ(counter, 1);
}

TEST(QTensor, QTensorSizingTest) {
  vector<int> dims(3);
  dims[0] = 2;
  dims[1] = 3;
  dims[2] = 5;
  QTensor<CPUContext> qtensor(dims, 3);
  EXPECT_TRUE(qtensor.mutable_data() != nullptr);
  EXPECT_EQ(qtensor.nbytes(), 12);
  EXPECT_EQ(qtensor.size(), 30);
}
} // namespace
} // namespace caffe2
