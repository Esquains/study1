#pragma once

#include "caffe2/core/operator.h"

namespace caffe2 {

template <typename Context>
void adam_update(
    int N,
    const float* g,
    const float* m,
    const float* v,
    float* ng,
    float* nm,
    float* nv,
    float beta1,
    float beta2,
    float eps_hat,
    float correction,
    const float* lr,
    Context* /*context*/) {
  for (auto i = 0; i < N; ++i) {
    float gi = g[i];
    float mi = nm[i] = m[i] * beta1 + gi * (1 - beta1);
    float vi = nv[i] = v[i] * beta2 + gi * gi * (1 - beta2);
    ng[i] = lr[0] * correction * mi / (std::sqrt(vi) + eps_hat);
  }
}

template <typename Context>
void adam_compute(
    int N,
    const float* w,
    const float* g,
    const float* m,
    const float* v,
    float* nw,
    float* nm,
    float* nv,
    float beta1,
    float beta2,
    float eps_hat,
    float correction,
    int apply_adagrad,
    int reset_aux_param,
    float adagrad_lr,
    float adagrad_epsilon,
    const float* lr,
    Context* /*context*/) {
  if (apply_adagrad == 1) {
    if (reset_aux_param == 0) {
      for (auto i = 0; i < N; ++i) {
        float gi = g[i];
        float vi = nv[i] = v[i] + gi * gi;
        nw[i] = w[i] + adagrad_lr * gi / (std::sqrt(vi) + adagrad_epsilon);
      }
    } else {
      for (auto i = 0; i < N; ++i) {
        float gi = g[i];
        float vi = nv[i] = gi * gi;
        nw[i] = w[i] + adagrad_lr * gi / (std::sqrt(vi) + adagrad_epsilon);
      }
    }
  } else {
    for (auto i = 0; i < N; ++i) {
      float gi = g[i];
      float mi = nm[i] = m[i] * beta1 + gi * (1 - beta1);
      float vi = nv[i] = v[i] * beta2 + gi * gi * (1 - beta2);
      nw[i] = w[i] + lr[0] * correction * mi / (std::sqrt(vi) + eps_hat);
    }
  }
}

template <typename Context>
void adam_compute_output_grad(
    int N,
    const float* w,
    const float* g,
    const float* m,
    const float* v,
    float* nw,
    float* nm,
    float* nv,
    float* ng,
    float beta1,
    float beta2,
    float eps_hat,
    float correction,
    int apply_adagrad,
    int reset_aux_param,
    float adagrad_lr,
    float adagrad_epsilon,
    const float* lr,
    Context* /*context*/) {
  if (apply_adagrad == 1) {
    if (reset_aux_param == 0) {
      for (auto i = 0; i < N; ++i) {
        float gi = g[i];
        float vi = nv[i] = v[i] + gi * gi;
        float ngi = ng[i] = gi / (std::sqrt(vi) + adagrad_epsilon);
        nw[i] = w[i] + adagrad_lr * ngi;
      }
    } else {
      for (auto i = 0; i < N; ++i) {
        float gi = g[i];
        float vi = nv[i] = gi * gi;
        float ngi = ng[i] = gi / (std::sqrt(vi) + adagrad_epsilon);
        nw[i] = w[i] + adagrad_lr * ngi;
      }
    }
  } else {
    for (auto i = 0; i < N; ++i) {
      float gi = g[i];
      float mi = nm[i] = m[i] * beta1 + gi * (1 - beta1);
      float vi = nv[i] = v[i] * beta2 + gi * gi * (1 - beta2);
      float ngi = ng[i] = correction * mi / (std::sqrt(vi) + eps_hat);
      nw[i] = w[i] + lr[0] * ngi;
    }
  }
}

template <typename Context>
void adam_update_output_grad_and_effective_lr(
    int N,
    const float* w,
    const float* g,
    const float* m,
    const float* v,
    float* nw,
    float* nm,
    float* nv,
    float* ng,
    float* effectiveLR,
    float beta1,
    float beta2,
    float eps_hat,
    float correction,
    int apply_adagrad,
    int reset_aux_param,
    float adagrad_lr,
    float adagrad_epsilon,
    const float* lr,
    Context* /*context*/) {
  if (apply_adagrad == 1) {
    if (reset_aux_param == 0) {
      for (auto i = 0; i < N; ++i) {
        float gi = g[i];
        float vi = nv[i] = v[i] + gi * gi;
        ng[i] = gi / (std::sqrt(vi) + adagrad_epsilon);
        float effectiveLRi = effectiveLR[i] =
            adagrad_lr / (std::sqrt(vi) + adagrad_epsilon);
        nw[i] = w[i] + effectiveLRi * gi;
      }
    } else {
      for (auto i = 0; i < N; ++i) {
        float gi = g[i];
        float vi = nv[i] = gi * gi;
        ng[i] = gi / (std::sqrt(vi) + adagrad_epsilon);
        float effectiveLRi = effectiveLR[i] =
            adagrad_lr / (std::sqrt(vi) + adagrad_epsilon);
        nw[i] = w[i] + effectiveLRi * gi;
      }
    }
  } else {
    for (auto i = 0; i < N; ++i) {
      float gi = g[i];
      float mi = nm[i] = m[i] * beta1 + gi * (1 - beta1);
      float vi = nv[i] = v[i] * beta2 + gi * gi * (1 - beta2);
      ng[i] = correction * mi / (std::sqrt(vi) + eps_hat);
      float effectiveLRi = effectiveLR[i] =
          lr[0] * correction / (std::sqrt(vi) + eps_hat);
      nw[i] = w[i] + effectiveLRi * mi;
    }
  }
}

template <typename Context>
void adam_update_output_grad_and_effective_lr_and_update(
    int N,
    const float* w,
    const float* g,
    const float* m,
    const float* v,
    float* nw,
    float* nm,
    float* nv,
    float* ng,
    float* effectiveLR,
    float* update,
    float beta1,
    float beta2,
    float eps_hat,
    float correction,
    int apply_adagrad,
    int reset_aux_param,
    float adagrad_lr,
    float adagrad_epsilon,
    const float* lr,
    Context* /*context*/) {
  if (apply_adagrad == 1) {
    if (reset_aux_param == 0) {
      for (auto i = 0; i < N; ++i) {
        float gi = g[i];
        float vi = nv[i] = v[i] + gi * gi;
        ng[i] = gi / (std::sqrt(vi) + adagrad_epsilon);
        float effectiveLRi = effectiveLR[i] =
            adagrad_lr / (std::sqrt(vi) + adagrad_epsilon);
        float updatei = update[i] = effectiveLRi * gi;
        nw[i] = w[i] + updatei;
      }
    } else {
      for (auto i = 0; i < N; ++i) {
        float gi = g[i];
        float vi = nv[i] = gi * gi;
        ng[i] = gi / (std::sqrt(vi) + adagrad_epsilon);
        float effectiveLRi = effectiveLR[i] =
            adagrad_lr / (std::sqrt(vi) + adagrad_epsilon);
        float updatei = update[i] = effectiveLRi * gi;
        nw[i] = w[i] + updatei;
      }
    }
  } else {
    for (auto i = 0; i < N; ++i) {
      float gi = g[i];
      float mi = nm[i] = m[i] * beta1 + gi * (1 - beta1);
      float vi = nv[i] = v[i] * beta2 + gi * gi * (1 - beta2);
      ng[i] = correction * mi / (std::sqrt(vi) + eps_hat);
      float effectiveLRi = effectiveLR[i] =
          lr[0] * correction / (std::sqrt(vi) + eps_hat);
      float updatei = update[i] = effectiveLRi * mi;
      nw[i] = w[i] + updatei;
    }
  }
}

template <typename T, class Context>
class AdamOp final : public Operator<Context> {
 public:
  USE_OPERATOR_CONTEXT_FUNCTIONS;
  AdamOp(const OperatorDef& operator_def, Workspace* ws)
      : Operator<Context>(operator_def, ws),
        beta1_(this->template GetSingleArgument<float>("beta1", 0.9f)),
        beta2_(this->template GetSingleArgument<float>("beta2", 0.999f)),
        epsilon_(this->template GetSingleArgument<float>("epsilon", 1e-5f)),
        composite_optimizer_(
            this->template GetSingleArgument<int>("composite_optimizer", 1)),
        initial_iter_(
            this->template GetSingleArgument<int>("initial_iter", -1)),
        adagrad_alpha_(
            this->template GetSingleArgument<float>("adagrad_alpha", 0.01f)),
        adagrad_epsilon_(
            this->template GetSingleArgument<float>("adagrad_epsilon", 0.1f)),
        load_aux_param_(
            this->template GetSingleArgument<bool>("load_aux_param", false)) {}
  bool RunOnDevice() override {
    // Iter live on the CPU
    CAFFE_ENFORCE(OperatorBase::InputIsTensorType(ITER, CPU));
    CAFFE_ENFORCE(Input(LR).numel() == 1);
    CAFFE_ENFORCE(Input(GRAD).numel() == Input(PARAM).numel());
    CAFFE_ENFORCE(Input(GRAD).numel() == Input(MOMENT_1).numel());
    CAFFE_ENFORCE(Input(GRAD).numel() == Input(MOMENT_2).numel());
    Output(OUTPUT_PARAM)->ResizeLike(Input(PARAM));
    Output(OUTPUT_MOMENT_1)->ResizeLike(Input(MOMENT_1));
    Output(OUTPUT_MOMENT_2)->ResizeLike(Input(MOMENT_2));

    const auto iter =
        OperatorBase::Input<Tensor>(ITER, CPU).template data<int64_t>()[0];

    const auto t = iter + 1;
    const auto correction =
        std::sqrt(T(1.) - std::pow(beta2_, t)) / (T(1.) - std::pow(beta1_, t));
    const auto apply_adagrad =
        (composite_optimizer_ == 2 && initial_iter_ > 0 && t >= initial_iter_);
    const auto reset_aux_param =
        (load_aux_param_ == false && t == initial_iter_);

    if (OutputSize() == 3) {
      adam_compute<Context>(
          Input(GRAD).numel(),
          Input(PARAM).template data<T>(),
          Input(GRAD).template data<T>(),
          Input(MOMENT_1).template data<T>(),
          Input(MOMENT_2).template data<T>(),
          Output(OUTPUT_PARAM)->template mutable_data<T>(),
          Output(OUTPUT_MOMENT_1)->template mutable_data<T>(),
          Output(OUTPUT_MOMENT_2)->template mutable_data<T>(),
          beta1_,
          beta2_,
          epsilon_,
          correction,
          apply_adagrad,
          reset_aux_param,
          adagrad_alpha_,
          adagrad_epsilon_,
          Input(LR).template data<T>(),
          &context_);
    } else if (OutputSize() == 4) {
      Output(OUTPUT_GRAD)->ResizeLike(Input(GRAD));
      adam_compute_output_grad<Context>(
          Input(GRAD).numel(),
          Input(PARAM).template data<T>(),
          Input(GRAD).template data<T>(),
          Input(MOMENT_1).template data<T>(),
          Input(MOMENT_2).template data<T>(),
          Output(OUTPUT_PARAM)->template mutable_data<T>(),
          Output(OUTPUT_MOMENT_1)->template mutable_data<T>(),
          Output(OUTPUT_MOMENT_2)->template mutable_data<T>(),
          Output(OUTPUT_GRAD)->template mutable_data<T>(),
          beta1_,
          beta2_,
          epsilon_,
          correction,
          apply_adagrad,
          reset_aux_param,
          adagrad_alpha_,
          adagrad_epsilon_,
          Input(LR).template data<T>(),
          &context_);
    } else if (OutputSize() == 5) {
      Output(OUTPUT_GRAD)->ResizeLike(Input(GRAD));
      Output(OUTPUT_EFFECTIVE_LR)->ResizeLike(Input(PARAM));
      adam_update_output_grad_and_effective_lr<Context>(
          Input(GRAD).numel(),
          Input(PARAM).template data<T>(),
          Input(GRAD).template data<T>(),
          Input(MOMENT_1).template data<T>(),
          Input(MOMENT_2).template data<T>(),
          Output(OUTPUT_PARAM)->template mutable_data<T>(),
          Output(OUTPUT_MOMENT_1)->template mutable_data<T>(),
          Output(OUTPUT_MOMENT_2)->template mutable_data<T>(),
          Output(OUTPUT_GRAD)->template mutable_data<T>(),
          Output(OUTPUT_EFFECTIVE_LR)->template mutable_data<T>(),
          beta1_,
          beta2_,
          epsilon_,
          correction,
          apply_adagrad,
          reset_aux_param,
          adagrad_alpha_,
          adagrad_epsilon_,
          Input(LR).template data<T>(),
          &context_);
    } else {
      Output(OUTPUT_GRAD)->ResizeLike(Input(GRAD));
      Output(OUTPUT_EFFECTIVE_LR)->ResizeLike(Input(PARAM));
      Output(OUTPUT_UPDATE)->ResizeLike(Input(PARAM));
      adam_update_output_grad_and_effective_lr_and_update<Context>(
          Input(GRAD).numel(),
          Input(PARAM).template data<T>(),
          Input(GRAD).template data<T>(),
          Input(MOMENT_1).template data<T>(),
          Input(MOMENT_2).template data<T>(),
          Output(OUTPUT_PARAM)->template mutable_data<T>(),
          Output(OUTPUT_MOMENT_1)->template mutable_data<T>(),
          Output(OUTPUT_MOMENT_2)->template mutable_data<T>(),
          Output(OUTPUT_GRAD)->template mutable_data<T>(),
          Output(OUTPUT_EFFECTIVE_LR)->template mutable_data<T>(),
          Output(OUTPUT_UPDATE)->template mutable_data<T>(),
          beta1_,
          beta2_,
          epsilon_,
          correction,
          apply_adagrad,
          reset_aux_param,
          adagrad_alpha_,
          adagrad_epsilon_,
          Input(LR).template data<T>(),
          &context_);
    }

    return true;
  }

 protected:
  T beta1_{0.9};
  T beta2_{0.999};
  T epsilon_{1e-8};
  T composite_optimizer_{1};
  T initial_iter_{-1};
  T adagrad_alpha_{0.01};
  T adagrad_epsilon_{1e-1};
  T load_aux_param_{false};
  INPUT_TAGS(PARAM, MOMENT_1, MOMENT_2, GRAD, LR, ITER);
  OUTPUT_TAGS(
      OUTPUT_PARAM,
      OUTPUT_MOMENT_1,
      OUTPUT_MOMENT_2,
      OUTPUT_GRAD,
      OUTPUT_EFFECTIVE_LR,
      OUTPUT_UPDATE);
};

template <typename T, class Context>
class SparseAdamOp final : public Operator<Context> {
 public:
  USE_OPERATOR_CONTEXT_FUNCTIONS;
  SparseAdamOp(const OperatorDef& operator_def, Workspace* ws)
      : Operator<Context>(operator_def, ws),
        beta1_(this->template GetSingleArgument<float>("beta1", 0.9f)),
        beta2_(this->template GetSingleArgument<float>("beta2", 0.999f)),
        epsilon_(this->template GetSingleArgument<float>("epsilon", 1e-5f)),
        composite_optimizer_(
            this->template GetSingleArgument<int>("composite_optimizer", 1)),
        initial_iter_(
            this->template GetSingleArgument<int>("initial_iter", -1)),
        adagrad_alpha_(
            this->template GetSingleArgument<float>("adagrad_alpha", 0.01f)),
        adagrad_epsilon_(
            this->template GetSingleArgument<float>("adagrad_epsilon", 0.1f)),
        load_aux_param_(
            this->template GetSingleArgument<bool>("load_aux_param", false)) {}

  bool RunOnDevice() override {
    // Enforce shapes
    CAFFE_ENFORCE_EQ(Input(PARAM).numel(), Input(MOMENT_1).numel());
    CAFFE_ENFORCE_EQ(Input(PARAM).numel(), Input(MOMENT_2).numel());
    CAFFE_ENFORCE_EQ(
        Input(PARAM).size_from_dim(1),
        Input(GRAD).size_from_dim(Input(INDICES).dim()));
    CAFFE_ENFORCE_EQ(Input(LR).numel(), 1);

    return DispatchHelper<TensorTypes<int32_t, int64_t>>::call(
        this, Input(INDICES));
  }

  template <typename SIndex>
  bool DoRunWithType() {
    const auto* lr = Input(LR).template data<T>();
    const auto iter =
        OperatorBase::Input<Tensor>(ITER, CPU).template data<int64_t>()[0];

    const auto t = iter + 1;
    const auto correction =
        std::sqrt(T(1.) - std::pow(beta2_, t)) / (T(1.) - std::pow(beta1_, t));
    const auto apply_adagrad =
        (composite_optimizer_ == 2 && initial_iter_ > 0 && t >= initial_iter_);
    const auto reset_aux_param =
        (load_aux_param_ == false && t == initial_iter_);

    const auto* paramIn = Input(PARAM).template data<T>();
    const auto* indices = Input(INDICES).template data<SIndex>();
    const auto* gradIn = Input(GRAD).template data<T>();
    const auto* moment1In = Input(MOMENT_1).template data<T>();
    const auto* moment2In = Input(MOMENT_2).template data<T>();
    auto* paramOut = Output(OUTPUT_PARAM)->template mutable_data<T>();
    auto* moment1Out = Output(OUTPUT_MOMENT_1)->template mutable_data<T>();
    auto* moment2Out = Output(OUTPUT_MOMENT_2)->template mutable_data<T>();

    if (OutputSize() == 3) {
      auto n = Input(INDICES).numel();
      if (n == 0) {
        return true;
      }

      auto block_size = Input(GRAD).numel() / n;
      for (auto i = 0; i < n; ++i) {
        auto idx = indices[i];

        if (block_size == 1) {
          if (apply_adagrad == 1) {
            if (reset_aux_param == 0) {
              float gi = gradIn[i];
              float vi = moment2Out[idx] = moment2In[idx] + gi * gi;
              paramOut[idx] = paramIn[idx] +
                  adagrad_alpha_ * gi / (std::sqrt(vi) + adagrad_epsilon_);
            } else {
              float gi = gradIn[i];
              float vi = moment2Out[idx] = gi * gi;
              paramOut[idx] = paramIn[idx] +
                  adagrad_alpha_ * gi / (std::sqrt(vi) + adagrad_epsilon_);
            }
          } else {
            float gi = gradIn[i];
            float mi = moment1Out[idx] =
                moment1In[idx] * beta1_ + gi * (1 - beta1_);
            float vi = moment2Out[idx] =
                moment2In[idx] * beta2_ + gi * gi * (1 - beta2_);
            paramOut[idx] = paramIn[idx] +
                lr[0] * correction * mi / (std::sqrt(vi) + epsilon_);
          }
        } else {
          auto offsetI = i * block_size;
          auto offsetIdx = idx * block_size;

#ifndef NDEBUG
          CAFFE_ENFORCE_GE(
              Input(PARAM).numel(),
              block_size + offsetIdx,
              this->debug_def().input(PARAM),
              ", out of bound,  idx:",
              idx,
              " for input i:",
              i,
              " and block size:",
              block_size);
          CAFFE_ENFORCE_GE(
              Input(GRAD).numel(),
              block_size + offsetI,
              this->debug_def().input(GRAD),
              ", out of bound idx, idx:",
              idx,
              " for input i:",
              i);
#endif

          adam_compute(
              block_size,
              paramIn + offsetIdx,
              gradIn + offsetI,
              moment1In + offsetIdx,
              moment2In + offsetIdx,
              paramOut + offsetIdx,
              moment1Out + offsetIdx,
              moment2Out + offsetIdx,
              beta1_,
              beta2_,
              epsilon_,
              correction,
              apply_adagrad,
              reset_aux_param,
              adagrad_alpha_,
              adagrad_epsilon_,
              lr,
              &context_);
        }
      }
    } else if (OutputSize() == 4) {
      Output(OUTPUT_GRAD)->ResizeLike(Input(GRAD));
      auto* gradOut = Output(OUTPUT_GRAD)->template mutable_data<T>();

      auto n = Input(INDICES).numel();
      if (n == 0) {
        return true;
      }

      auto block_size = Input(GRAD).numel() / n;
      for (auto i = 0; i < n; ++i) {
        auto idx = indices[i];

        if (block_size == 1) {
          if (apply_adagrad == 1) {
            if (reset_aux_param == 0) {
              float gi = gradIn[i];
              float vi = moment2Out[idx] = moment2In[idx] + gi * gi;
              float ngi = gradOut[i] = gi / (std::sqrt(vi) + adagrad_epsilon_);
              paramOut[idx] = paramIn[idx] + adagrad_alpha_ * ngi;
            } else {
              float gi = gradIn[i];
              float vi = moment2Out[idx] = gi * gi;
              float ngi = gradOut[i] = gi / (std::sqrt(vi) + adagrad_epsilon_);
              paramOut[idx] = paramIn[idx] + adagrad_alpha_ * ngi;
            }
          } else {
            float gi = gradIn[i];
            float mi = moment1Out[idx] =
                moment1In[idx] * beta1_ + gi * (1 - beta1_);
            float vi = moment2Out[idx] =
                moment2In[idx] * beta2_ + gi * gi * (1 - beta2_);
            float ngi = gradOut[i] =
                correction * mi / (std::sqrt(vi) + epsilon_);
            paramOut[idx] = paramIn[idx] + lr[0] * ngi;
          }
        } else {
          auto offsetI = i * block_size;
          auto offsetIdx = idx * block_size;

#ifndef NDEBUG
          CAFFE_ENFORCE_GE(
              Input(PARAM).numel(),
              block_size + offsetIdx,
              this->debug_def().input(PARAM),
              ", out of bound,  idx:",
              idx,
              " for input i:",
              i,
              " and block size:",
              block_size);
          CAFFE_ENFORCE_GE(
              Input(GRAD).numel(),
              block_size + offsetI,
              this->debug_def().input(GRAD),
              ", out of bound idx, idx:",
              idx,
              " for input i:",
              i);
#endif

          adam_compute_output_grad(
              block_size,
              paramIn + offsetIdx,
              gradIn + offsetI,
              moment1In + offsetIdx,
              moment2In + offsetIdx,
              paramOut + offsetIdx,
              moment1Out + offsetIdx,
              moment2Out + offsetIdx,
              gradOut + offsetI,
              beta1_,
              beta2_,
              epsilon_,
              correction,
              apply_adagrad,
              reset_aux_param,
              adagrad_alpha_,
              adagrad_epsilon_,
              lr,
              &context_);
        }
      }
    } else if (OutputSize() == 5) {
      Output(OUTPUT_GRAD)->ResizeLike(Input(GRAD));
      Output(OUTPUT_EFFECTIVE_LR)->ResizeLike(Input(PARAM));
      auto* gradOut = Output(OUTPUT_GRAD)->template mutable_data<T>();
      auto* effectivelrOut =
          Output(OUTPUT_EFFECTIVE_LR)->template mutable_data<T>();

      auto n = Input(INDICES).numel();
      if (n == 0) {
        return true;
      }

      auto block_size = Input(GRAD).numel() / n;
      for (auto i = 0; i < n; ++i) {
        auto idx = indices[i];

        if (block_size == 1) {
          if (apply_adagrad == 1) {
            if (reset_aux_param == 0) {
              float gi = gradIn[i];
              float vi = moment2Out[idx] = moment2In[idx] + gi * gi;
              gradOut[i] = gi / (std::sqrt(vi) + adagrad_epsilon_);
              float ei = effectivelrOut[idx] =
                  adagrad_alpha_ / (std::sqrt(vi) + adagrad_epsilon_);
              paramOut[idx] = paramIn[idx] + ei * gi;
            } else {
              float gi = gradIn[i];
              float vi = moment2Out[idx] = gi * gi;
              gradOut[i] = gi / (std::sqrt(vi) + adagrad_epsilon_);
              float ei = effectivelrOut[idx] =
                  adagrad_alpha_ / (std::sqrt(vi) + adagrad_epsilon_);
              paramOut[idx] = paramIn[idx] + ei * gi;
            }
          } else {
            float gi = gradIn[i];
            float mi = moment1Out[idx] =
                moment1In[idx] * beta1_ + gi * (1 - beta1_);
            float vi = moment2Out[idx] =
                moment2In[idx] * beta2_ + gi * gi * (1 - beta2_);
            gradOut[i] = correction * mi / (std::sqrt(vi) + epsilon_);
            float ei = effectivelrOut[idx] =
                lr[0] * correction / (std::sqrt(vi) + epsilon_);
            paramOut[idx] = paramIn[idx] + ei * mi;
          }
        } else {
          auto offsetI = i * block_size;
          auto offsetIdx = idx * block_size;

#ifndef NDEBUG
          CAFFE_ENFORCE_GE(
              Input(PARAM).numel(),
              block_size + offsetIdx,
              this->debug_def().input(PARAM),
              ", out of bound,  idx:",
              idx,
              " for input i:",
              i,
              " and block size:",
              block_size);
          CAFFE_ENFORCE_GE(
              Input(GRAD).numel(),
              block_size + offsetI,
              this->debug_def().input(GRAD),
              ", out of bound idx, idx:",
              idx,
              " for input i:",
              i);
#endif

          adam_update_output_grad_and_effective_lr(
              block_size,
              paramIn + offsetIdx,
              gradIn + offsetI,
              moment1In + offsetIdx,
              moment2In + offsetIdx,
              paramOut + offsetIdx,
              moment1Out + offsetIdx,
              moment2Out + offsetIdx,
              gradOut + offsetI,
              effectivelrOut + offsetIdx,
              beta1_,
              beta2_,
              epsilon_,
              correction,
              apply_adagrad,
              reset_aux_param,
              adagrad_alpha_,
              adagrad_epsilon_,
              lr,
              &context_);
        }
      }
    } else {
      Output(OUTPUT_GRAD)->ResizeLike(Input(GRAD));
      Output(OUTPUT_EFFECTIVE_LR)->ResizeLike(Input(PARAM));
      Output(OUTPUT_UPDATE)->ResizeLike(Input(PARAM));
      auto* gradOut = Output(OUTPUT_GRAD)->template mutable_data<T>();
      auto* effectivelrOut =
          Output(OUTPUT_EFFECTIVE_LR)->template mutable_data<T>();
      auto* updateOut = Output(OUTPUT_UPDATE)->template mutable_data<T>();

      auto n = Input(INDICES).numel();
      if (n == 0) {
        return true;
      }

      auto block_size = Input(GRAD).numel() / n;
      for (auto i = 0; i < n; ++i) {
        auto idx = indices[i];

        if (block_size == 1) {
          if (apply_adagrad == 1) {
            if (reset_aux_param == 0) {
              float gi = gradIn[i];
              float vi = moment2Out[idx] = moment2In[idx] + gi * gi;
              gradOut[i] = gi / (std::sqrt(vi) + adagrad_epsilon_);
              float ei = effectivelrOut[idx] =
                  adagrad_alpha_ / (std::sqrt(vi) + adagrad_epsilon_);
              float ui = updateOut[idx] = ei * gi;
              paramOut[idx] = paramIn[idx] + ui;
            } else {
              float gi = gradIn[i];
              float vi = moment2Out[idx] = moment2In[idx] + gi * gi;
              gradOut[i] = gi / (std::sqrt(vi) + adagrad_epsilon_);
              float ei = effectivelrOut[idx] =
                  adagrad_alpha_ / (std::sqrt(vi) + adagrad_epsilon_);
              float ui = updateOut[idx] = ei * gi;
              paramOut[idx] = paramIn[idx] + ui;
            }
          } else {
            float gi = gradIn[i];
            float mi = moment1Out[idx] =
                moment1In[idx] * beta1_ + gi * (1 - beta1_);
            float vi = moment2Out[idx] =
                moment2In[idx] * beta2_ + gi * gi * (1 - beta2_);
            gradOut[i] = correction * mi / (std::sqrt(vi) + epsilon_);
            float ei = effectivelrOut[idx] =
                lr[0] * correction / (std::sqrt(vi) + epsilon_);
            float ui = updateOut[idx] = ei * mi;
            paramOut[idx] = paramIn[idx] + ui;
          }

        } else {
          auto offsetI = i * block_size;
          auto offsetIdx = idx * block_size;

#ifndef NDEBUG
          CAFFE_ENFORCE_GE(
              Input(PARAM).numel(),
              block_size + offsetIdx,
              this->debug_def().input(PARAM),
              ", out of bound,  idx:",
              idx,
              " for input i:",
              i,
              " and block size:",
              block_size);
          CAFFE_ENFORCE_GE(
              Input(GRAD).numel(),
              block_size + offsetI,
              this->debug_def().input(GRAD),
              ", out of bound idx, idx:",
              idx,
              " for input i:",
              i);
#endif

          adam_update_output_grad_and_effective_lr_and_update(
              block_size,
              paramIn + offsetIdx,
              gradIn + offsetI,
              moment1In + offsetIdx,
              moment2In + offsetIdx,
              paramOut + offsetIdx,
              moment1Out + offsetIdx,
              moment2Out + offsetIdx,
              gradOut + offsetI,
              effectivelrOut + offsetIdx,
              updateOut + offsetIdx,
              beta1_,
              beta2_,
              epsilon_,
              correction,
              apply_adagrad,
              reset_aux_param,
              adagrad_alpha_,
              adagrad_epsilon_,
              lr,
              &context_);
        }
      }
    }
    return true;
  }

 protected:
  T beta1_;
  T beta2_;
  T epsilon_;
  T composite_optimizer_{1};
  T initial_iter_{-1};
  T adagrad_alpha_{0.01};
  T adagrad_epsilon_{1e-1};
  T load_aux_param_{false};
  INPUT_TAGS(PARAM, MOMENT_1, MOMENT_2, INDICES, GRAD, LR, ITER);
  OUTPUT_TAGS(
      OUTPUT_PARAM,
      OUTPUT_MOMENT_1,
      OUTPUT_MOMENT_2,
      OUTPUT_GRAD,
      OUTPUT_EFFECTIVE_LR,
      OUTPUT_UPDATE);
};

template <typename T, class Context>
class RowWiseSparseAdamOp final : public Operator<Context> {
 public:
  USE_OPERATOR_CONTEXT_FUNCTIONS;
  RowWiseSparseAdamOp(const OperatorDef& operator_def, Workspace* ws)
      : Operator<Context>(operator_def, ws),
        beta1_(this->template GetSingleArgument<float>("beta1", 0.9f)),
        beta2_(this->template GetSingleArgument<float>("beta2", 0.999f)),
        epsilon_(this->template GetSingleArgument<float>("epsilon", 1e-5f)) {}

  bool RunOnDevice() override {
    // Enforce shapes
    CAFFE_ENFORCE_EQ(Input(PARAM).numel(), Input(MOMENT_1).numel());
    CAFFE_ENFORCE_EQ(Input(PARAM).sizes()[0], Input(MOMENT_2).numel());
    CAFFE_ENFORCE_EQ(
        Input(PARAM).size_from_dim(1),
        Input(GRAD).size_from_dim(Input(INDICES).dim()));
    CAFFE_ENFORCE_EQ(Input(LR).numel(), 1);

    return DispatchHelper<TensorTypes<int32_t, int64_t>>::call(
        this, Input(INDICES));
  }

  template <typename SIndex>
  bool DoRunWithType() {
    const auto* lr = Input(LR).template data<T>();
    const auto iter =
        OperatorBase::Input<Tensor>(ITER, CPU).template data<int64_t>()[0];

    const auto t = iter + 1;
    const auto correction =
        std::sqrt(T(1.) - std::pow(beta2_, t)) / (T(1.) - std::pow(beta1_, t));

    auto block_size = Input(PARAM).numel() / Input(PARAM).size(0);
    auto n = Input(GRAD).numel() / block_size;

    const auto* paramIn = Input(PARAM).template data<T>();
    const auto* indices = Input(INDICES).template data<SIndex>();
    const auto* gradIn = Input(GRAD).template data<T>();
    const auto* moment1In = Input(MOMENT_1).template data<T>();
    const auto* moment2In = Input(MOMENT_2).template data<T>();
    auto* paramOut = Output(OUTPUT_PARAM)->template mutable_data<T>();
    auto* moment1Out = Output(OUTPUT_MOMENT_1)->template mutable_data<T>();
    auto* moment2Out = Output(OUTPUT_MOMENT_2)->template mutable_data<T>();

    if (OutputSize() == 3) {
      for (auto i = 0; i < n; ++i) {
        auto idx = indices[i];

        if (block_size == 1) {
          float gi = gradIn[i];
          float mi = moment1Out[idx] =
              moment1In[idx] * beta1_ + gi * (1 - beta1_);
          float vi = moment2Out[idx] =
              moment2In[idx] * beta2_ + gi * gi * (1 - beta2_);
          paramOut[idx] = paramIn[idx] +
              lr[0] * correction * mi / (std::sqrt(vi) + epsilon_);

        } else {
          auto offsetI = i * block_size;
          auto offsetIdx = idx * block_size;

#ifndef NDEBUG
          CAFFE_ENFORCE_GE(
              Input(PARAM).numel(),
              block_size + offsetIdx,
              this->debug_def().input(PARAM),
              ", out of bound,  idx:",
              idx,
              " for input i:",
              i,
              " and block size:",
              block_size);
          CAFFE_ENFORCE_GE(
              Input(GRAD).numel(),
              block_size + offsetI,
              this->debug_def().input(GRAD),
              ", out of bound idx, idx:",
              idx,
              " for input i:",
              i);
#endif

          const float* w = paramIn + offsetIdx;
          const float* g = gradIn + offsetI;
          const float* m1 = moment1In + offsetIdx;
          const float* m2 = moment2In + idx;
          float* nw = paramOut + offsetIdx;
          float* nm1 = moment1Out + offsetIdx;
          float* nm2 = moment2Out + idx;

          float m2_sum = 0.;
          for (auto j = 0; j < block_size; ++j) {
            float gj = g[j];
            m2_sum += gj * gj;
          }
          float vi = nm2[0] =
              m2[0] * beta2_ + (m2_sum / block_size) * (1 - beta2_);
          for (auto j = 0; j < block_size; ++j) {
            float mi = nm1[j] = m1[j] * beta1_ + g[j] * (1 - beta1_);
            nw[j] = w[j] + lr[0] * correction * mi / (std::sqrt(vi) + epsilon_);
          }
        }
      }
    } else {
      Output(OUTPUT_GRAD)->ResizeLike(Input(GRAD));
      auto* gradOut = Output(OUTPUT_GRAD)->template mutable_data<T>();
      for (auto i = 0; i < n; ++i) {
        auto idx = indices[i];

        if (block_size == 1) {
          float gi = gradIn[i];
          float mi = moment1Out[idx] =
              moment1In[idx] * beta1_ + gi * (1 - beta1_);
          float vi = moment2Out[idx] =
              moment2In[idx] * beta2_ + gi * gi * (1 - beta2_);
          float ngi = gradOut[i] = correction * mi / (std::sqrt(vi) + epsilon_);
          paramOut[idx] = paramIn[idx] + lr[0] * ngi;

        } else {
          auto offsetI = i * block_size;
          auto offsetIdx = idx * block_size;

#ifndef NDEBUG
          CAFFE_ENFORCE_GE(
              Input(PARAM).numel(),
              block_size + offsetIdx,
              this->debug_def().input(PARAM),
              ", out of bound,  idx:",
              idx,
              " for input i:",
              i,
              " and block size:",
              block_size);
          CAFFE_ENFORCE_GE(
              Input(GRAD).numel(),
              block_size + offsetI,
              this->debug_def().input(GRAD),
              ", out of bound idx, idx:",
              idx,
              " for input i:",
              i);
#endif

          const float* w = paramIn + offsetIdx;
          const float* g = gradIn + offsetI;
          const float* m1 = moment1In + offsetIdx;
          const float* m2 = moment2In + idx;
          float* nw = paramOut + offsetIdx;
          float* nm1 = moment1Out + offsetIdx;
          float* nm2 = moment2Out + idx;
          float* ng = gradOut + offsetI;

          float m2_sum = 0.;
          for (auto j = 0; j < block_size; ++j) {
            float gj = g[j];
            m2_sum += gj * gj;
          }
          float vi = nm2[0] =
              m2[0] * beta2_ + (m2_sum / block_size) * (1 - beta2_);
          for (auto j = 0; j < block_size; ++j) {
            float mi = nm1[j] = m1[j] * beta1_ + g[j] * (1 - beta1_);
            float ngi = ng[j] = correction * mi / (std::sqrt(vi) + epsilon_);
            nw[j] = w[j] + lr[0] * ngi;
          }
        }
      }
    }
    return true;
  }

 protected:
  T beta1_;
  T beta2_;
  T epsilon_;
  INPUT_TAGS(PARAM, MOMENT_1, MOMENT_2, INDICES, GRAD, LR, ITER);
  OUTPUT_TAGS(OUTPUT_PARAM, OUTPUT_MOMENT_1, OUTPUT_MOMENT_2, OUTPUT_GRAD);
};

} // namespace caffe2
