#include <gmp.h>

#include "tensorflow/core/framework/op.h"
#include "tensorflow/core/framework/op_kernel.h"
#include "tensorflow/core/framework/shape_inference.h"
#include "tensorflow/core/framework/tensor_util.h"
#include "tensorflow/core/framework/variant.h"
#include "tensorflow/core/framework/variant_encode_decode.h"
#include "tensorflow/core/framework/variant_op_registry.h"
#include "tensorflow/core/framework/variant_tensor_data.h"
#include "tf_big/cc/big_tensor.h"

using namespace tensorflow;  // NOLINT
using tf_big::BigTensor;

Status GetBigTensor(OpKernelContext* ctx, int index, const BigTensor** res) {
  const Tensor& input = ctx->input(index);

  const BigTensor* big = input.flat<Variant>()(0).get<BigTensor>();

  if (big == nullptr) {
    return errors::InvalidArgument("Input handle is not a big tensor. Saw: '",
                                   input.flat<Variant>()(0).DebugString(), "'");
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
    OP_REQUIRES_OK(ctx, ctx->allocate_output(0, input.shape(), &val));

    BigTensor big;
    big.FromTensor<T>(input);

    val->flat<Variant>()(0) = std::move(big);
  }
};

template <typename T>
class BigImportLimbsOp : public OpKernel {
 public:
  explicit BigImportLimbsOp(OpKernelConstruction* context)
      : OpKernel(context) {}

  void Compute(OpKernelContext* ctx) override {
    const Tensor& input = ctx->input(0);
    OP_REQUIRES(ctx, TensorShapeUtils::IsMatrixOrHigher(input.shape()),
                errors::InvalidArgument(
                    "value expected to be at least a matrix ",
                    "but got shape: ", input.shape().DebugString()));

    Tensor* val;
    OP_REQUIRES_OK(ctx,
                   ctx->allocate_output(0,
                                        TensorShape{input.shape().dim_size(0),
                                                    input.shape().dim_size(1)},
                                        &val));

    BigTensor big;
    big.LimbsFromTensor<T>(input);

    val->flat<Variant>()(0) = std::move(big);
  }
};

template <typename T>
class BigExportOp : public OpKernel {
 public:
  explicit BigExportOp(OpKernelConstruction* context) : OpKernel(context) {}

  void Compute(OpKernelContext* ctx) override {
    const BigTensor* val = nullptr;
    TensorShape input_shape = ctx->input(0).shape();
    OP_REQUIRES_OK(ctx, GetBigTensor(ctx, 0, &val));

    Tensor* output;
    OP_REQUIRES_OK(ctx, ctx->allocate_output(0, input_shape, &output));

    val->ToTensor<T>(output);
  }
};

template <typename T>
class BigExportLimbsOp : public OpKernel {
 public:
  explicit BigExportLimbsOp(OpKernelConstruction* context)
      : OpKernel(context) {}

  void Compute(OpKernelContext* ctx) override {
    const Tensor& maxval_tensor = ctx->input(0);
    auto max_bitlen = maxval_tensor.flat<int32>()(0);

    const BigTensor* curBigTensor = nullptr;
    TensorShape input_shape = ctx->input(1).shape();
    OP_REQUIRES_OK(ctx, GetBigTensor(ctx, 1, &curBigTensor));

    Tensor* output;

    TensorShape output_shape;
    output_shape.AddDim(input_shape.dim_size(0));
    output_shape.AddDim(input_shape.dim_size(1));

    unsigned int type_bitlen = sizeof(T) * 8;
    unsigned int len_field_bits = 32;
    unsigned int num_max_limbs =
        (len_field_bits + max_bitlen + type_bitlen - 1) / type_bitlen;

    output_shape.AddDim(num_max_limbs);

    OP_REQUIRES_OK(ctx, ctx->allocate_output(0, output_shape, &output));

    auto rows = curBigTensor->value.rows();
    auto cols = curBigTensor->value.cols();

    auto flatened = output->flat<T>();
    uint8_t* result = reinterpret_cast<uint8_t*>(flatened.data());

    size_t expansion_factor = output->dim_size(2) * sizeof(T);
    size_t header_len = 4;
    size_t pointer = 0;

    for (int i = 0; i < rows; i++) {
      for (int j = 0; j < cols; j++) {
        unsigned int num =
            mpz_sizeinbase(curBigTensor->value(i, j).get_mpz_t(), 256);
        tf_big::encode_length(result + pointer, num);

        size_t exported_len;
        mpz_export(result + pointer + header_len, &exported_len, 1,
                   sizeof(uint8_t), 0, 0,
                   curBigTensor->value(i, j).get_mpz_t());
        OP_REQUIRES(
            ctx, header_len + exported_len <= expansion_factor,
            errors::Internal("User selected wrong byte length, required: ",
                             exported_len, " bytes"));
        for (size_t k = header_len + exported_len; k < expansion_factor; k++)
          result[pointer + k] = 0;
        pointer += expansion_factor;
      }
    }
  }
};

class BigAddOp : public OpKernel {
 public:
  explicit BigAddOp(OpKernelConstruction* context) : OpKernel(context) {}

  void Compute(OpKernelContext* ctx) override {
    const BigTensor* val0 = nullptr;
    OP_REQUIRES_OK(ctx, GetBigTensor(ctx, 0, &val0));

    const BigTensor* val1 = nullptr;
    OP_REQUIRES_OK(ctx, GetBigTensor(ctx, 1, &val1));

    Tensor* output;
    OP_REQUIRES_OK(ctx, ctx->allocate_output(0, val0->shape(), &output));

    auto res = *val0 + *val1;

    output->flat<Variant>()(0) = std::move(res);
  }
};

class BigSubOp : public OpKernel {
 public:
  explicit BigSubOp(OpKernelConstruction* context) : OpKernel(context) {}

  void Compute(OpKernelContext* ctx) override {
    const BigTensor* val0 = nullptr;
    OP_REQUIRES_OK(ctx, GetBigTensor(ctx, 0, &val0));

    const BigTensor* val1 = nullptr;
    OP_REQUIRES_OK(ctx, GetBigTensor(ctx, 1, &val1));

    Tensor* output;
    OP_REQUIRES_OK(ctx, ctx->allocate_output(0, val0->shape(), &output));

    auto res = *val0 - *val1;

    output->flat<Variant>()(0) = std::move(res);
  }
};

class BigMulOp : public OpKernel {
 public:
  explicit BigMulOp(OpKernelConstruction* context) : OpKernel(context) {}

  void Compute(OpKernelContext* ctx) override {
    const BigTensor* val0 = nullptr;
    OP_REQUIRES_OK(ctx, GetBigTensor(ctx, 0, &val0));

    const BigTensor* val1 = nullptr;
    OP_REQUIRES_OK(ctx, GetBigTensor(ctx, 1, &val1));

    Tensor* output;
    OP_REQUIRES_OK(ctx, ctx->allocate_output(0, val0->shape(), &output));

    auto res = (*val0).cwiseProduct(*val1);

    output->flat<Variant>()(0) = std::move(res);
  }
};

class BigDivOp : public OpKernel {
 public:
  explicit BigDivOp(OpKernelConstruction* context) : OpKernel(context) {}

  void Compute(OpKernelContext* ctx) override {
    const BigTensor* val0 = nullptr;
    OP_REQUIRES_OK(ctx, GetBigTensor(ctx, 0, &val0));

    const BigTensor* val1 = nullptr;
    OP_REQUIRES_OK(ctx, GetBigTensor(ctx, 1, &val1));

    Tensor* output;
    OP_REQUIRES_OK(ctx, ctx->allocate_output(0, val0->shape(), &output));

    auto res = (*val0).cwiseQuotient(*val1);

    output->flat<Variant>()(0) = std::move(res);
  }
};

class BigPowOp : public OpKernel {
 public:
  explicit BigPowOp(OpKernelConstruction* ctx) : OpKernel(ctx) {
    OP_REQUIRES_OK(ctx, ctx->GetAttr("secure", &secure));
  }

  void Compute(OpKernelContext* ctx) override {
    const BigTensor* base = nullptr;
    OP_REQUIRES_OK(ctx, GetBigTensor(ctx, 0, &base));

    const BigTensor* exponent_t = nullptr;
    OP_REQUIRES_OK(ctx, GetBigTensor(ctx, 1, &exponent_t));

    // TODO(Morten) modulus should be optional
    const BigTensor* modulus_t = nullptr;
    OP_REQUIRES_OK(ctx, GetBigTensor(ctx, 2, &modulus_t));

    Tensor* output;
    OP_REQUIRES_OK(ctx, ctx->allocate_output(0, base->shape(), &output));

    auto exponent = exponent_t->value.data();
    auto modulus = modulus_t->value(0, 0);

    MatrixXm res(base->rows(), base->cols());
    auto v = base->value.data();
    auto size = base->value.size();

    mpz_t tmp;
    mpz_init(tmp);
    for (int i = 0; i < size; i++) {
      if (secure) {
        mpz_powm_sec(tmp, v[i].get_mpz_t(), exponent[i].get_mpz_t(),
                     modulus.get_mpz_t());
      } else {
        mpz_powm(tmp, v[i].get_mpz_t(), exponent[i].get_mpz_t(),
                 modulus.get_mpz_t());
      }

      res.data()[i] = mpz_class(tmp);
    }
    mpz_clear(tmp);

    output->flat<Variant>()(0) = BigTensor(res);
  }

 private:
  bool secure = false;
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
    OP_REQUIRES_OK(
        ctx, ctx->allocate_output(0, TensorShape{val1->rows(), val2->cols()},
                                  &output));

    auto res = *val1 * *val2;

    output->flat<Variant>()(0) = std::move(res);
  }
};

class BigModOp : public OpKernel {
 public:
  explicit BigModOp(OpKernelConstruction* context) : OpKernel(context) {}

  void Compute(OpKernelContext* ctx) override {
    const BigTensor* val = nullptr;
    OP_REQUIRES_OK(ctx, GetBigTensor(ctx, 0, &val));

    const BigTensor* mod = nullptr;
    OP_REQUIRES_OK(ctx, GetBigTensor(ctx, 1, &mod));
    auto modulus = mod->value(0, 0);

    MatrixXm res_matrix(val->rows(), val->cols());
    auto res_data = res_matrix.data();
    auto val_data = val->value.data();
    auto size = val->value.size();

    mpz_t tmp;
    mpz_init(tmp);
    for (int i = 0; i < size; i++) {
      mpz_mod(tmp, val_data[i].get_mpz_t(), modulus.get_mpz_t());
      res_data[i] = mpz_class(tmp);
    }
    mpz_clear(tmp);

    Tensor* res;
    OP_REQUIRES_OK(ctx, ctx->allocate_output(0, val->shape(), &res));
    res->flat<Variant>()(0) = BigTensor(res_matrix);
  }
};

class BigInvOp : public OpKernel {
 public:
  explicit BigInvOp(OpKernelConstruction* context) : OpKernel(context) {}

  void Compute(OpKernelContext* ctx) override {
    const BigTensor* val = nullptr;
    OP_REQUIRES_OK(ctx, GetBigTensor(ctx, 0, &val));

    const BigTensor* mod = nullptr;
    OP_REQUIRES_OK(ctx, GetBigTensor(ctx, 1, &mod));
    auto modulus = mod->value(0, 0);

    MatrixXm res_matrix(val->rows(), val->cols());
    auto res_data = res_matrix.data();
    auto val_data = val->value.data();
    auto size = val->value.size();

    mpz_t tmp;
    mpz_init(tmp);
    for (int i = 0; i < size; i++) {
      mpz_invert(tmp, val_data[i].get_mpz_t(), modulus.get_mpz_t());
      res_data[i] = mpz_class(tmp);
    }
    mpz_clear(tmp);

    Tensor* res;
    OP_REQUIRES_OK(ctx, ctx->allocate_output(0, val->shape(), &res));
    res->flat<Variant>()(0) = BigTensor(res_matrix);
  }
};

class BigRandomUniformOp : public OpKernel {
 public:
  explicit BigRandomUniformOp(OpKernelConstruction* context)
      : OpKernel(context) {}

  void Compute(OpKernelContext* ctx) override {
    const Tensor& shape_tensor = ctx->input(0);
    TensorShape shape;
    OP_REQUIRES_OK(ctx, tensor::MakeShape(shape_tensor, &shape));

    const BigTensor* maxval_tensor = nullptr;
    OP_REQUIRES_OK(ctx, GetBigTensor(ctx, 1, &maxval_tensor));
    auto maxval = maxval_tensor->value(0, 0).get_mpz_t();

    MatrixXm res_matrix(shape.dim_size(0), shape.dim_size(1));
    auto res_data = res_matrix.data();
    auto size = res_matrix.size();

    // TODO(Morten) offer secure randomness
    gmp_randstate_t state;
    tf_big::gmp_utils::init_randstate(state);
    mpz_t tmp;
    mpz_init(tmp);
    for (int i = 0; i < size; i++) {
      mpz_urandomm(tmp, state, maxval);
      res_data[i] = mpz_class(tmp);
    }
    mpz_clear(tmp);

    Tensor* res;
    OP_REQUIRES_OK(ctx, ctx->allocate_output(0, shape, &res));
    res->flat<Variant>()(0) = BigTensor(res_matrix);
  }
};

class BigRandomRsaModulusOp : public OpKernel {
 public:
  explicit BigRandomRsaModulusOp(OpKernelConstruction* context)
      : OpKernel(context) {}

  void Compute(OpKernelContext* ctx) override {
    const Tensor& bitlength_t = ctx->input(0);
    auto bitlength = bitlength_t.scalar<int32>();
    auto bitlength_val = bitlength.data();

    MatrixXm p_matrix(1, 1);
    auto p_data = p_matrix.data();
    MatrixXm q_matrix(1, 1);
    auto q_data = q_matrix.data();
    MatrixXm n_matrix(1, 1);
    auto n_data = n_matrix.data();

    gmp_randstate_t state;
    tf_big::gmp_utils::init_randstate(state);
    mpz_t p;
    mpz_t q;
    mpz_t n;
    mpz_init(p);
    mpz_init(q);
    mpz_init(n);

    do {
      do {
        mpz_urandomb(p, state, *bitlength_val / 2);
      } while (!mpz_probab_prime_p(p, 10));

      do {
        mpz_urandomb(q, state, *bitlength_val / 2);
      } while (!mpz_probab_prime_p(q, 10));

      mpz_mul(n, p, q);
    } while (!mpz_tstbit(n, *bitlength_val - 1));

    p_data[0] = mpz_class(p);
    q_data[0] = mpz_class(q);
    n_data[0] = mpz_class(n);

    TensorShape shape({1, 1});
    Tensor* p_res;
    OP_REQUIRES_OK(ctx, ctx->allocate_output(0, shape, &p_res));
    p_res->flat<Variant>()(0) = BigTensor(p_matrix);

    Tensor* q_res;
    OP_REQUIRES_OK(ctx, ctx->allocate_output(1, shape, &q_res));
    q_res->flat<Variant>()(0) = BigTensor(q_matrix);

    Tensor* n_res;
    OP_REQUIRES_OK(ctx, ctx->allocate_output(2, shape, &n_res));
    n_res->flat<Variant>()(0) = BigTensor(n_matrix);

    mpz_clear(p);
    mpz_clear(q);
    mpz_clear(n);
  }
};

REGISTER_UNARY_VARIANT_DECODE_FUNCTION(BigTensor, BigTensor::kTypeName);

REGISTER_KERNEL_BUILDER(
    Name("BigImport").Device(DEVICE_CPU).TypeConstraint<tstring>("dtype"),
    BigImportOp<tstring>);
REGISTER_KERNEL_BUILDER(
    Name("BigImport").Device(DEVICE_CPU).TypeConstraint<int32>("dtype"),
    BigImportOp<int32>);
REGISTER_KERNEL_BUILDER(
    Name("BigImport").Device(DEVICE_CPU).TypeConstraint<uint8>("dtype"),
    BigImportOp<uint8>);

REGISTER_KERNEL_BUILDER(
    Name("BigExport").Device(DEVICE_CPU).TypeConstraint<tstring>("dtype"),
    BigExportOp<tstring>);
REGISTER_KERNEL_BUILDER(
    Name("BigExport").Device(DEVICE_CPU).TypeConstraint<int32>("dtype"),
    BigExportOp<int32>);
REGISTER_KERNEL_BUILDER(
    Name("BigExport").Device(DEVICE_CPU).TypeConstraint<uint8>("dtype"),
    BigExportOp<uint8>);

REGISTER_KERNEL_BUILDER(
    Name("BigImportLimbs").Device(DEVICE_CPU).TypeConstraint<int32>("dtype"),
    BigImportLimbsOp<int32>);
REGISTER_KERNEL_BUILDER(
    Name("BigImportLimbs").Device(DEVICE_CPU).TypeConstraint<uint8>("dtype"),
    BigImportLimbsOp<uint8>);

REGISTER_KERNEL_BUILDER(
    Name("BigExportLimbs").Device(DEVICE_CPU).TypeConstraint<int32>("dtype"),
    BigExportLimbsOp<int32>);
REGISTER_KERNEL_BUILDER(
    Name("BigExportLimbs").Device(DEVICE_CPU).TypeConstraint<uint8>("dtype"),
    BigExportLimbsOp<uint8>);

// TODO(justin1121) there's no simple mpz to int64 convert functions
// there's a suggestion here (https://stackoverflow.com/a/6248913/1116574) on
// how to do it but it might take a bit of investigation from our side
// perhaps arguable that we should only export/import to string for safety
// can convert to number in python????
// REGISTER_CPU(int64);

REGISTER_KERNEL_BUILDER(Name("BigRandomUniform").Device(DEVICE_CPU),
                        BigRandomUniformOp);
REGISTER_KERNEL_BUILDER(Name("BigRandomRsaModulus").Device(DEVICE_CPU),
                        BigRandomRsaModulusOp);

REGISTER_KERNEL_BUILDER(Name("BigAdd").Device(DEVICE_CPU), BigAddOp);
REGISTER_KERNEL_BUILDER(Name("BigSub").Device(DEVICE_CPU), BigSubOp);
REGISTER_KERNEL_BUILDER(Name("BigMul").Device(DEVICE_CPU), BigMulOp);
REGISTER_KERNEL_BUILDER(Name("BigDiv").Device(DEVICE_CPU), BigDivOp);
REGISTER_KERNEL_BUILDER(Name("BigPow").Device(DEVICE_CPU), BigPowOp);
REGISTER_KERNEL_BUILDER(Name("BigMatMul").Device(DEVICE_CPU), BigMatMulOp);
REGISTER_KERNEL_BUILDER(Name("BigMod").Device(DEVICE_CPU), BigModOp);
REGISTER_KERNEL_BUILDER(Name("BigInv").Device(DEVICE_CPU), BigInvOp);
