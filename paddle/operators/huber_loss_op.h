/* Copyright (c) 2016 PaddlePaddle Authors. All Rights Reserve.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License. */

#pragma once
#include "paddle/framework/eigen.h"
#include "paddle/framework/op_registry.h"
#include "paddle/platform/hostdevice.h"

namespace paddle {
namespace operators {

using Tensor = framework::Tensor;
template <typename T, int MajorType = Eigen::RowMajor,
          typename IndexType = Eigen::DenseIndex>
using EigenVector = framework::EigenVector<T, MajorType, IndexType>;

template <typename T>
struct HuberLossForward {
  HOSTDEVICE HuberLossForward(const T& delta) : delta(delta) {}

  HOSTDEVICE T operator()(const T& val) const {
    T abs_val = std::abs(val);
    if (abs_val <= delta) {
      return 0.5 * val * val;
    } else {
      return delta * (abs_val - 0.5 * delta);
    }
  }

  T delta;
};

template <typename Place, typename T, typename AttrType = T>
class HuberLossKernel : public framework::OpKernel {
 public:
  void Compute(const framework::ExecutionContext& context) const override {
    auto* in0 = context.Input<Tensor>("X");
    auto* in1 = context.Input<Tensor>("Y");
    auto* out0 = context.Output<Tensor>("Residual");
    auto* out1 = context.Output<Tensor>("Out");
    auto delta = static_cast<T>(context.op().Attr<AttrType>("delta"));
    auto place = context.GetEigenDevice<Place>();

    auto x = EigenVector<T>::Flatten(*in0);
    auto y = EigenVector<T>::Flatten(*in1);
    out0->mutable_data<T>(context.GetPlace());
    auto residual = EigenVector<T>::Flatten(*out0);
    residual.device(place) = y - x;
    out1->mutable_data<T>(context.GetPlace());
    auto loss = EigenVector<T>::Flatten(*out1);
    loss.device(place) = residual.unaryExpr(HuberLossForward<T>(delta));
  }
};

template <typename T>
struct HuberLossBackward {
  HOSTDEVICE HuberLossBackward(const T& delta, bool is_x)
      : is_x(is_x), delta(delta) {}

  HOSTDEVICE T operator()(const T& val) const {
    T sign = is_x ? -1.0 : 1.0;
    T abs_val = std::abs(val);
    if (abs_val <= delta) {
      return sign * val;
    } else {
      if (val > 0) {
        return sign * delta;
      } else {
        return -1 * sign * delta;
      }
    }
  }

  bool is_x;
  T delta;
};

template <typename Place, typename T, typename AttrType = T>
class HuberLossGradKernel : public framework::OpKernel {
 public:
  void Compute(const framework::ExecutionContext& context) const override {
    auto* in0 = context.Input<Tensor>("Residual");
    auto* in1 = context.Input<Tensor>(framework::GradVarName("Out"));
    auto* out0 = context.Output<Tensor>(framework::GradVarName("X"));
    auto* out1 = context.Output<Tensor>(framework::GradVarName("Y"));
    auto delta = static_cast<T>(context.op().Attr<AttrType>("delta"));
    auto place = context.GetEigenDevice<Place>();

    auto residual = EigenVector<T>::Flatten(*in0);
    auto out_grad = EigenVector<T>::Flatten(*in1);

    if (out0) {
      out0->mutable_data<T>(context.GetPlace());
      auto x_grad = EigenVector<T>::Flatten(*out0);
      x_grad.device(place) =
          out_grad * residual.unaryExpr(HuberLossBackward<T>(delta, true));
    }

    if (out1) {
      out1->mutable_data<T>(context.GetPlace());
      auto y_grad = EigenVector<T>::Flatten(*out1);
      y_grad.device(place) =
          out_grad * residual.unaryExpr(HuberLossBackward<T>(delta, false));
    }
  }
};

}  // namespace operators
}  // namespace paddle
