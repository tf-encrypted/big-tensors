#include <gmp.h>

#include "tensorflow/core/framework/op.h"
#include "tensorflow/core/framework/op_kernel.h"
#include "tensorflow/core/framework/shape_inference.h"
#include "tensorflow/core/framework/variant.h"
#include "tensorflow/core/framework/variant_encode_decode.h"
#include "tensorflow/core/framework/variant_op_registry.h"
#include "tensorflow/core/framework/variant_tensor_data.h"
#include "tensorflow/core/kernels/bounds_check.h"

#include "tf_big/cc/kernels/big_tensor.h"

using namespace tensorflow;  // NOLINT
using tf_big::BigTensor;

Status GetBigTensor(OpKernelContext* ctx, int index, const BigTensor** res) {
  const Tensor& input = ctx->input(index);

  // TODO(justin1121): check scalar type
  const BigTensor* big = input.scalar<Variant>()().get<BigTensor>();
  if (big == nullptr) {
    return errors::InvalidArgument("Input handle is not a big tensor. Saw: '",
                                   input.scalar<Variant>()().DebugString(),
                                   "'");
  }

  *res = big;
  return Status::OK();
}

template <typename T>
class BigImportOp : public OpKernel {
 public:
  explicit BigImportOp(OpKernelConstruction* context) : OpKernel(context) {}

  void Compute(OpKernelContext* ctx) override {
    const Tensor& input = ctx->input(0);
    OP_REQUIRES(ctx, TensorShapeUtils::IsMatrix(input.shape()),
                errors::InvalidArgument(
                    "value expected to be a matrix ",
                    "but got shape: ", input.shape().DebugString()));

    Tensor* val;
    OP_REQUIRES_OK(ctx, ctx->allocate_output(0, TensorShape{}, &val));

    BigTensor big;
    big.FromTensor<T>(input);

    val->scalar<Variant>()() = std::move(big);
  }
};

template <typename T>
class BigExportOp : public OpKernel {
 public:
  explicit BigExportOp(OpKernelConstruction* context) : OpKernel(context) {}

  void Compute(OpKernelContext* ctx) override {
    const BigTensor* val = nullptr;
    OP_REQUIRES_OK(ctx, GetBigTensor(ctx, 0, &val));
    auto output_shape = TensorShape{val->rows(), val->cols()};

    Tensor* output;
    OP_REQUIRES_OK(ctx, ctx->allocate_output(0, output_shape, &output));

    val->ToTensor<T>(output);
  }
};

class BigAddOp : public OpKernel {
 public:
  explicit BigAddOp(OpKernelConstruction* context) : OpKernel(context) {}

  void Compute(OpKernelContext* ctx) override {
    const BigTensor* val1 = nullptr;
    OP_REQUIRES_OK(ctx, GetBigTensor(ctx, 0, &val1));

    const BigTensor* val2 = nullptr;
    OP_REQUIRES_OK(ctx, GetBigTensor(ctx, 1, &val2));

    Tensor* output;
    OP_REQUIRES_OK(ctx, ctx->allocate_output(0, TensorShape{}, &output));

    auto res = *val1 + *val2;

    output->scalar<Variant>()() = std::move(res);
  }
};

class BigMulOp : public OpKernel {
 public:
  explicit BigMulOp(OpKernelConstruction* context) : OpKernel(context) {}

  void Compute(OpKernelContext* ctx) override {
    const BigTensor* val1 = nullptr;
    OP_REQUIRES_OK(ctx, GetBigTensor(ctx, 0, &val1));

    const BigTensor* val2 = nullptr;
    OP_REQUIRES_OK(ctx, GetBigTensor(ctx, 1, &val2));

    Tensor* output;
    OP_REQUIRES_OK(ctx, ctx->allocate_output(0, TensorShape{}, &output));

    auto res = (*val1).cwiseProduct(*val2);

    output->scalar<Variant>()() = std::move(res);
  }
};

class BigMatMulOp : public OpKernel {
 public:
  explicit BigMatMulOp(OpKernelConstruction* context) : OpKernel(context) {}

  void Compute(OpKernelContext* ctx) override {
    const BigTensor* val1 = nullptr;
    OP_REQUIRES_OK(ctx, GetBigTensor(ctx, 0, &val1));

    const BigTensor* val2 = nullptr;
    OP_REQUIRES_OK(ctx, GetBigTensor(ctx, 1, &val2));

    Tensor* output;
    OP_REQUIRES_OK(ctx, ctx->allocate_output(0, TensorShape{}, &output));

    auto res = *val1 * *val2;

    output->scalar<Variant>()() = std::move(res);
  }
};

// Register the CPU kernels.
#define REGISTER_CPU(T)                                                \
  REGISTER_KERNEL_BUILDER(                                             \
      Name("BigImport").Device(DEVICE_CPU).TypeConstraint<T>("dtype"), \
      BigImportOp<T>);                                                 \
  REGISTER_KERNEL_BUILDER(                                             \
      Name("BigExport").Device(DEVICE_CPU).TypeConstraint<T>("dtype"), \
      BigExportOp<T>);

REGISTER_CPU(string);
REGISTER_CPU(int32);

// TODO(justin1121) there's no simple mpz to int64 convert functions
// there's a suggestion here (https://stackoverflow.com/a/6248913/1116574) on
// how to do it but it might take a bit of investigation from our side
// perhaps arguable that we should only export/import to string for safety
// can convert to number in python????
// REGISTER_CPU(int64);

REGISTER_UNARY_VARIANT_DECODE_FUNCTION(BigTensor, BigTensor::kTypeName);

REGISTER_KERNEL_BUILDER(Name("BigAdd").Device(DEVICE_CPU), BigAddOp);
REGISTER_KERNEL_BUILDER(Name("BigMatMul").Device(DEVICE_CPU), BigMatMulOp);
REGISTER_KERNEL_BUILDER(Name("BigMul").Device(DEVICE_CPU), BigMulOp);
