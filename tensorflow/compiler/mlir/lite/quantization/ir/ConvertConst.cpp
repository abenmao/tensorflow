/* Copyright 2022 The TensorFlow Authors. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/

#include "third_party/llvm/llvm-project/mlir/include/mlir/Dialect/Arithmetic/IR/Arithmetic.h"
#include "third_party/llvm/llvm-project/mlir/include/mlir/Dialect/Quant/QuantTypes.h"
#include "third_party/llvm/llvm-project/mlir/include/mlir/IR/BuiltinTypes.h"
#include "third_party/llvm/llvm-project/mlir/include/mlir/IR/Matchers.h"
#include "third_party/llvm/llvm-project/mlir/include/mlir/Transforms/GreedyPatternRewriteDriver.h"
#include "third_party/tensorflow/compiler/mlir/lite/quantization/ir/PassDetail.h"
#include "third_party/tensorflow/compiler/mlir/lite/quantization/ir/Passes.h"
#include "third_party/tensorflow/compiler/mlir/lite/quantization/ir/QuantOps.h"
#include "third_party/tensorflow/compiler/mlir/lite/quantization/ir/QuantizeUtils.h"
#include "third_party/tensorflow/compiler/mlir/lite/quantization/ir/UniformSupport.h"

using namespace mlir;
using namespace mlir::quantfork;

using mlir::quant::QuantizedType;

namespace {
struct ConvertConstPass : public QuantConvertConstBase<ConvertConstPass> {
  void runOnOperation() override;
};

struct QuantizedConstRewrite : public OpRewritePattern<QuantizeCastOp> {
  using OpRewritePattern<QuantizeCastOp>::OpRewritePattern;

  LogicalResult matchAndRewrite(QuantizeCastOp qbarrier,
                                PatternRewriter &rewriter) const override;
};

}  // namespace

/// Matches a [constant] -> [qbarrier] where the qbarrier results type is
/// quantized and the operand type is quantizable.

LogicalResult QuantizedConstRewrite::matchAndRewrite(
    QuantizeCastOp qbarrier, PatternRewriter &rewriter) const {
  Attribute value;

  // Is the operand a constant?
  if (!matchPattern(qbarrier.getArg(), m_Constant(&value))) {
    return failure();
  }

  // Does the qbarrier convert to a quantized type. This will not be true
  // if a quantized type has not yet been chosen or if the cast to an equivalent
  // storage type is not supported.
  Type qbarrierResultType = qbarrier.getResult().getType();
  QuantizedType quantizedElementType =
      QuantizedType::getQuantizedElementType(qbarrierResultType);
  if (!quantizedElementType) {
    return failure();
  }
  if (!QuantizedType::castToStorageType(qbarrierResultType)) {
    return failure();
  }

  // Is the operand type compatible with the expressed type of the quantized
  // type? This will not be true if the qbarrier is superfluous (converts
  // from and to a quantized type).
  if (!quantizedElementType.isCompatibleExpressedType(
          qbarrier.getArg().getType())) {
    return failure();
  }

  // Is the constant value a type expressed in a way that we support?
  if (!value.isa<FloatAttr, DenseElementsAttr, SparseElementsAttr>()) {
    return failure();
  }

  Type newConstValueType;
  auto newConstValue =
      quantizeAttr(value, quantizedElementType, newConstValueType);
  if (!newConstValue) {
    return failure();
  }

  // When creating the new const op, use a fused location that combines the
  // original const and the qbarrier that led to the quantization.
  auto fusedLoc = rewriter.getFusedLoc(
      {qbarrier.getArg().getDefiningOp()->getLoc(), qbarrier.getLoc()});
  auto newConstOp = rewriter.create<arith::ConstantOp>(
      fusedLoc, newConstValueType, newConstValue);
  rewriter.replaceOpWithNewOp<StorageCastOp>(qbarrier, qbarrier.getType(),
                                             newConstOp);
  return success();
}

void ConvertConstPass::runOnOperation() {
  RewritePatternSet patterns(&getContext());
  auto func = getOperation();
  auto *context = &getContext();
  patterns.add<QuantizedConstRewrite>(context);
  (void)applyPatternsAndFoldGreedily(func, std::move(patterns));
}

std::unique_ptr<OperationPass<func::FuncOp>>
mlir::quantfork::createConvertConstPass() {
  return std::make_unique<ConvertConstPass>();
}
