// Copyright 2019 Google LLC
//
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree.

#include <assert.h>
#include <stddef.h>

#include <arm_neon.h>

#include <xnnpack/common.h>
#include <xnnpack/math-stubs.h>


// Table of exp2(k / 2048) values, k = 0..2047
extern XNN_INTERNAL const float xnn_table_exp2_k_over_2048[2048];

void xnn_math_f32_expminus__neonfma_lut2048_p1(
    size_t n,
    const float* input,
    float* output)
{
  assert(n % (4 * sizeof(float)) == 0);

  const float32x4_t vmagic_bias = vmovq_n_f32(0x1.800000p23f);
  // The smallest x for which expf(x) is normalized.
  const float32x4_t vdenorm_cutoff = vmovq_n_f32(-0x1.5D589Ep6f);
  const float32x4_t vlog2e_x2048  = vmovq_n_f32(0x1.715476p11f);
  const float32x4_t vminus_ln2_o2048_hi = vmovq_n_f32(-0x1.62e43p-12f);
  const float32x4_t vminus_ln2_o2048_lo = vmovq_n_f32(0x1.05c61p-40f);

  const float32x4_t vc1 = vmovq_n_f32(0x1.FFFFFEp-1f);

  const int32x4_t vindex_mask = vmovq_n_s32(INT32_C(0x7FF));

  for (; n != 0; n -= 4 * sizeof(float)) {
    const float32x4_t vx = vld1q_f32(input); input += 4;

    // Compute reduced argument n := round(x * 2048 / log(2)).
    // We do it by adding a large number (magic bias), which cause rounding of the result to an integer, then subtracing
    // the large number back. The first addition is combined with multiplication by log2e into a single FMA instruction.
    // The trick with adding large number is valid only within certain bounds (|x * 2048 / log(2)| <= 2**22, i.e.
    // |x| <= 0x1.62E43p+10 = 1419.5654296875), but that is acceptable, because inputs outside of [-87.336540, 0.0]
    // result in denormalized or underflown expf(x). We fixup the result for such inputs at the very end of the
    // algorithm.
    float32x4_t vn = vfmaq_f32(vmagic_bias, vx, vlog2e_x2048);

    // Create a floating-point number s (scale) such that s := 2**(n / 2048) for such inputs that expf(x) is normalized,
    // i.e. -87.33642 <= x <= 0.0. As n has 11 fractional bits, we split s == 2**(n / 2048) = 2**e * 2**(n / 2048 - e),
    // where e := int(n / 2048). We create s in two steps:
    // 1. Fetch 2**(n / 2048 - e) = 2**(n % 2048) from the table using the 6 low bits of n, as integer. Note that the
    //    fetched values are in the [1.0, 2.0) range, i.e. their floating-point exponent is 0.
    // 2. Adjust fecthed value by addition of e to its floating-point exponent. The result is always a normalized
    //    number, because for -87.33642 <= x <= 0.0 (inputs for which expf(x) is normalized) we have -126 <= e <= 0,
    //    and thus the adjusted exponent is not lower than -126.
    //
    // Extract e from bits 11:19 of n and shift it into bits 23:31 (position of floating-point exponent).
    const int32x4_t ve = vshlq_n_s32(vbicq_s32(vreinterpretq_s32_f32(vn), vmovq_n_s32(INT32_C(0x7FF))), 12);

    // Use bits 0:11 bits of n, as integer, as an index for table lookup of l := 2**(n % 2048).
    const uint64x2_t vidx = vreinterpretq_u64_s32(vandq_s32(vreinterpretq_s32_f32(vn), vindex_mask));
    const uint64_t vidx01 = vgetq_lane_u64(vidx, 0);
    const uint64_t vidx23 = vgetq_lane_u64(vidx, 1);
    float32x2_t vl01 = vld1_dup_f32(&xnn_table_exp2_k_over_2048[(uint32_t) vidx01]);
    float32x2_t vl23 = vld1_dup_f32(&xnn_table_exp2_k_over_2048[(uint32_t) vidx23]);
    vl01 = vld1_lane_f32(&xnn_table_exp2_k_over_2048[(uint32_t) (vidx01 >> 32)], vl01, 1);
    vl23 = vld1_lane_f32(&xnn_table_exp2_k_over_2048[(uint32_t) (vidx23 >> 32)], vl23, 1);
    const float32x4_t vl = vcombine_f32(vl01, vl23);
    // Adjust exponent of the value l fetched from the table to get the final s value.
    const float32x4_t vs = vreinterpretq_f32_s32(vaddq_s32(vreinterpretq_s32_f32(vl), ve));

    // Subtract the large number back to get final n := round(x * 2048 / log(2)) as a floating-point number.
    vn = vsubq_f32(vn, vmagic_bias);

    // Compute reduced argument t := x - n * log(2) / 2048.
    // Use Cody-Waite range reduction method (note the two constants representing log(2) / 2048) to improve accuracy.
    float32x4_t vt = vfmaq_f32(vx, vn, vminus_ln2_o2048_hi);
    vt = vfmaq_f32(vt, vn, vminus_ln2_o2048_lo);

    // Compute degree-1 polynomial approxiatmion for exp(t) on [-log(2)/2048, log(2)/2048].
    const float32x4_t vp = vmulq_f32(vt, vc1);

    // Reconstruct the final f value:
    //   f = s * (1 + t * c1)
    //     = s + s * (t * c1))
    //     = s + s * p
    float32x4_t vf = vfmaq_f32(vs, vs, vp);

    // For inputs below denormal cutoff, replace output with +0.0f.
    // Note that for NaN inputs, comparison result is false, and outputs are left unchanged.
    vf = vreinterpretq_f32_u32(vbicq_u32(vreinterpretq_u32_f32(vf), vcltq_f32(vx, vdenorm_cutoff)));
    vst1q_f32(output, vf); output += 4;
  }
}
