# Copyright 2020 Google LLC
#
# This source code is licensed under the BSD-style license found in the
# LICENSE file in the root directory of this source tree.

# void xnn_f32_relu_ukernel__wasm32_shr_x1(
#     size_t n,             0
#     const float* x,       1
#     float* y,             2
#     const union params)   3 unused

# locals
#     float v               4
#     float mask            5

    .text
    .section    .text.xnn_f32_relu_ukernel__wasm32_shr_x1,"",@
    .hidden     xnn_f32_relu_ukernel__wasm32_shr_x1
    .globl      xnn_f32_relu_ukernel__wasm32_shr_x1
    .type       xnn_f32_relu_ukernel__wasm32_shr_x1,@function

xnn_f32_relu_ukernel__wasm32_shr_x1:
    .functype   xnn_f32_relu_ukernel__wasm32_shr_x1 (i32, i32, i32, i32) -> ()
    .local      i32, i32  # 4 - value, 5 - mask

    loop

      local.get    1        # src
      i32.load     0        # load float from src
      local.set    4

      local.get    4        # (v >> 31) - 1) & v
      i32.const    31
      i32.shr_u
      local.set    5

      local.get    5
      i32.const    -1
      i32.add
      local.set    5

      local.get    4
      local.get    5
      i32.and
      local.set    4

      local.get    2        # dst
      local.get    4
      i32.store    0        # store float

      local.get    2        # dst += 4
      i32.const    4
      i32.add
      local.set    2

      local.get    1        # src += 4
      i32.const    4
      i32.add
      local.set    1

      local.get    0
      i32.const    -4
      i32.add              # count -= 4
      local.set    0

      local.get    0
      i32.const    0       # count > 0
      i32.gt_u
      br_if        0       # loop

    end_loop
    end_function
