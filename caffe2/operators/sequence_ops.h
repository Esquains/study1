/**
 * Copyright (c) 2016-present, Facebook, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef CAFFE2_OPERATORS_SEQUENCE_OPS_H_
#define CAFFE2_OPERATORS_SEQUENCE_OPS_H_

#include "caffe2/core/operator.h"
#include "caffe2/core/tensor.h"

namespace caffe2 {

template <class Context>
class GatherPaddingOp final : public Operator<Context> {
 public:
  USE_OPERATOR_CONTEXT_FUNCTIONS;
  GatherPaddingOp(const OperatorDef& operator_def, Workspace* ws)
      : Operator<Context>(operator_def, ws),
        startPaddingWidth_(
            OperatorBase::GetSingleArgument<int>("padding_width", 1)),
        endPaddingWidth_(
            OperatorBase::GetSingleArgument<int>("end_padding_width", -1)) {
    CAFFE_ENFORCE_GE(startPaddingWidth_, 0);
    if (endPaddingWidth_ < 0) {
      endPaddingWidth_ = startPaddingWidth_;
    }
  }

  bool RunOnDevice() override {
    if (startPaddingWidth_ == 0 && endPaddingWidth_ == 0) {
      Output(0)->Resize(std::vector<TIndex>(0));
      if (OutputSize() == 2) {
        Output(1)->Resize(std::vector<TIndex>(0));
      }
      return true;
    }
    return DispatchHelper<TensorTypes<float, double, int, int64_t, bool>>::call(
        this, Input(0));
  }

  template <typename T>
  bool DoRunWithType();

 private:
  int startPaddingWidth_;
  int endPaddingWidth_;
};

template <class Context>
class RemovePaddingOp final : public Operator<Context> {
 public:
  USE_OPERATOR_CONTEXT_FUNCTIONS;
  RemovePaddingOp(const OperatorDef& operator_def, Workspace* ws)
      : Operator<Context>(operator_def, ws),
        startPaddingWidth_(
            OperatorBase::GetSingleArgument<int>("padding_width", 1)),
        endPaddingWidth_(
            OperatorBase::GetSingleArgument<int>("end_padding_width", -1)) {
    CAFFE_ENFORCE_GE(startPaddingWidth_, 0);
    if (endPaddingWidth_ < 0) {
      endPaddingWidth_ = startPaddingWidth_;
    }
  }

  bool RunOnDevice() override {
    if (startPaddingWidth_ == 0 && endPaddingWidth_ == 0) {
      Output(0)->CopyFrom(Input(0), &context_);
      if (OutputSize() == 2) {
        Output(1)->CopyFrom(Input(1), &context_);
      }
      return true;
    }
    return DispatchHelper<TensorTypes<float, double, int, int64_t, bool>>::call(
        this, Input(0));
  }

  template <typename T>
  bool DoRunWithType();

 private:
  int startPaddingWidth_;
  int endPaddingWidth_;

  // Scratch space required by the CUDA version
  Tensor<Context> lengths_prefix_sum_buffer_;
  Tensor<Context> lengths_prefix_sum_;
};

template <class Context>
class AddPaddingOp final : public Operator<Context> {
 public:
  USE_OPERATOR_CONTEXT_FUNCTIONS;
  AddPaddingOp(const OperatorDef& operator_def, Workspace* ws)
      : Operator<Context>(operator_def, ws),
        startPaddingWidth_(
            OperatorBase::GetSingleArgument<int>("padding_width", 1)),
        endPaddingWidth_(
            OperatorBase::GetSingleArgument<int>("end_padding_width", -1)) {
    CAFFE_ENFORCE_GE(startPaddingWidth_, 0);
    if (endPaddingWidth_ < 0) {
      endPaddingWidth_ = startPaddingWidth_;
    }
  }

  bool RunOnDevice() override {
    if (startPaddingWidth_ == 0 && endPaddingWidth_ == 0) {
      Output(0)->CopyFrom(Input(0), &context_);
      if (OutputSize() == 2) {
        Output(1)->CopyFrom(Input(1), &context_);
      }
      return true;
    }
    return DispatchHelper<TensorTypes<float, double, int, int64_t, bool>>::call(
        this, Input(0));
  }

  template <typename T>
  bool DoRunWithType() {
    const auto& in = Input(0);
    CAFFE_ENFORCE_GE(in.ndim(), 1);
    const int32_t outer_size = in.dims()[0];
    const auto block_size = in.size_from_dim(1);

    // if no lengths is provided, assume it is a single full-span entry
    const int32_t* lengths_ptr = nullptr;
    int32_t lengths_size = 1;
    if (InputSize() > 1) {
      const auto& lengths = Input(1);
      lengths_ptr = lengths.template data<int32_t>();
      lengths_size = lengths.size();
    }

    // fetch paddings
    // input_size == 2 : pad with zeros
    // input_size == 3 : start and end paddings are the same
    // input_size == 4 : different start and end paddings
    const T* padding_start_ptr = nullptr;
    const T* padding_end_ptr = nullptr;
    if (InputSize() >= 3) {
      auto& padding_start = Input(2);
      CAFFE_ENFORCE_EQ(block_size, padding_start.size());
      padding_start_ptr = padding_start.template data<T>();
    }
    if (InputSize() == 4) {
      auto& padding_end = Input(3);
      CAFFE_ENFORCE_EQ(block_size, padding_end.size());
      padding_end_ptr = padding_end.template data<T>();
    } else {
      padding_end_ptr = padding_start_ptr;
    }

    auto* out = Output(0);
    {
      auto out_dims = in.dims();
      out_dims[0] += (startPaddingWidth_ + endPaddingWidth_) * lengths_size;
      out->Resize(std::move(out_dims));
    }
    const auto* in_ptr = in.template data<T>();
    auto* out_ptr = out->template mutable_data<T>();

    return MakePadding<T>(
        in_ptr,
        out_ptr,
        lengths_ptr,
        lengths_size,
        outer_size,
        padding_start_ptr,
        padding_end_ptr,
        block_size);
  }

  template <typename T>
  bool MakePadding(
      const T* in_ptr,
      T* out_ptr,
      const int32_t* lengths_ptr,
      int32_t lengths_size,
      int32_t outer_size,
      const T* padding_start_ptr,
      const T* padding_end_ptr,
      int64_t block_size);

 private:
  int startPaddingWidth_;
  int endPaddingWidth_;

  // Scratch space required by the CUDA version
  Tensor<Context> lengths_prefix_sum_buffer_;
  Tensor<Context> lengths_prefix_sum_;
};

template <class Context>
class PadEmptySamplesOp : public Operator<Context> {
 public:
  USE_OPERATOR_CONTEXT_FUNCTIONS;
  PadEmptySamplesOp(const OperatorDef& operator_def, Workspace* ws)
      : Operator<Context>(operator_def, ws) {}

  bool RunOnDevice() override;
};

} // namespace caffe2

#endif // CAFFE2_OPERATORS_SEQUENCE_OPS_H_
