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

#include <string.h>
#include "neon_util.h"

namespace paddle {

namespace neon {

#if defined(__ARM_NEON__) || defined(__ARM_NEON)

template <int filterSize, int stride>
struct DepthwiseConvKernel {};

inline float32_t conv3x3(float32x4_t r0,
                         float32x4_t r1,
                         float32x4_t r2,
                         float32x4_t k0,
                         float32x4_t k1,
                         float32x4_t k2) {
  float32x4_t tmp;
  tmp = vmulq_f32(r0, k0);
  tmp = vmlaq_f32(tmp, r1, k1);
  tmp = vmlaq_f32(tmp, r2, k2);
  return vaddvq_f32(tmp);
}

inline float32_t conv4x4(float32x4_t r0,
                         float32x4_t r1,
                         float32x4_t r2,
                         float32x4_t r3,
                         float32x4_t k0,
                         float32x4_t k1,
                         float32x4_t k2,
                         float32x4_t k3) {
  float32x4_t tmp;
  tmp = vmulq_f32(r0, k0);
  tmp = vmlaq_f32(tmp, r1, k1);
  tmp = vmlaq_f32(tmp, r2, k2);
  tmp = vmlaq_f32(tmp, r3, k3);
  return vaddvq_f32(tmp);
}

/**
 * Each step calculates four elements of the output.
 * First step:
 *   R0[0, 1, 2, 3...] * K[0][0]
 *   R0[1, 2, 3, 4...] * K[0][1]
 *   R0[2, 3, 4, 5...] * K[0][2]
 *   R1[0, 1, 2, 3...] * K[1][0]
 *   R1[1, 2, 3, 4...] * K[1][1]
 *   R1[2, 3, 4, 5...] * K[1][2]
 *   R2[0, 1, 2, 3...] * K[2][0]
 *   R2[1, 2, 3, 4...] * K[2][1]
 * + R2[2, 3, 4, 5...] * K[2][2]
 * ------------------------------
 *     Output[0, 1, 2, 3]
 */
template <>
struct DepthwiseConvKernel<3, 1> {
  static void run(const float* inputData,
                  const float* filterData,
                  int inputHeight,
                  int inputWidth,
                  int outputChannels,
                  int outputHeight,
                  int outputWidth,
                  int filterMultiplier,
                  float* outputData) {
    const int steps = outputWidth >> 2;
    const int remain = outputWidth & 3;
    for (int c = 0; c < outputChannels; c++, filterData += 9) {
      // Load the filters
      float32x4_t k[3];
      k[0] = vld1q_f32(filterData);
      k[1] = vld1q_f32(filterData + 3);
      k[2] = vld1q_f32(filterData + 6);
      k[0] = vsetq_lane_f32(0.f, k[0], 3);
      k[1] = vsetq_lane_f32(0.f, k[1], 3);
      k[2] = vsetq_lane_f32(0.f, k[2], 3);

      const float* r0 =
          inputData + (c / filterMultiplier) * (inputHeight * inputWidth);
      const float* r1 = r0 + inputWidth;
      const float* r2 = r0 + inputWidth * 2;
      float32x4_t input[3][3];
      for (int h = 0; h < outputHeight; h++) {
        for (int s = 0; s < steps; s++) {
          // Load the inputs
          float32x4_t tmp;
          input[0][0] = vld1q_f32(r0);
          tmp = vld1q_f32(r0 + 4);
          input[0][1] = vextq_f32(input[0][0], tmp, 1);
          input[0][2] = vextq_f32(input[0][0], tmp, 2);
          input[1][0] = vld1q_f32(r1);
          tmp = vld1q_f32(r1 + 4);
          input[1][1] = vextq_f32(input[1][0], tmp, 1);
          input[1][2] = vextq_f32(input[1][0], tmp, 2);
          input[2][0] = vld1q_f32(r2);
          tmp = vld1q_f32(r2 + 4);
          input[2][1] = vextq_f32(input[2][0], tmp, 1);
          input[2][2] = vextq_f32(input[2][0], tmp, 2);

          float32x4_t tmp1 = vdupq_n_f32(0.f);
          float32x4_t tmp2 = vdupq_n_f32(0.f);
          tmp1 = vmlaq_laneq_f32(tmp1, input[0][0], k[0], 0);
          tmp2 = vmlaq_laneq_f32(tmp2, input[0][1], k[0], 1);
          tmp1 = vmlaq_laneq_f32(tmp1, input[0][2], k[0], 2);
          tmp2 = vmlaq_laneq_f32(tmp2, input[1][0], k[1], 0);
          tmp1 = vmlaq_laneq_f32(tmp1, input[1][1], k[1], 1);
          tmp2 = vmlaq_laneq_f32(tmp2, input[1][2], k[1], 2);
          tmp1 = vmlaq_laneq_f32(tmp1, input[2][0], k[2], 0);
          tmp2 = vmlaq_laneq_f32(tmp2, input[2][1], k[2], 1);
          tmp1 = vmlaq_laneq_f32(tmp1, input[2][2], k[2], 2);
          tmp1 = vaddq_f32(tmp1, tmp2);

          vst1q_f32(outputData, tmp1);
          r0 += 4;
          r1 += 4;
          r2 += 4;
          outputData += 4;
        }

        for (int r = 0; r < remain; r++) {
          float32x4_t i0 = vld1q_f32(r0);
          float32x4_t i1 = vld1q_f32(r1);
          float32x4_t i2 = vld1q_f32(r2);
          *outputData = conv3x3(i0, i1, i2, k[0], k[1], k[2]);
          r0++;
          r1++;
          r2++;
          outputData++;
        }

        r0 += 2;
        r1 += 2;
        r2 += 2;
      }
    }
  }
};

/**
 * Each step calculates four elements of the output.
 * First step:
 *   R0[0, 2, 4, 6...] * K[0][0]
 *   R0[1, 3, 5, 7...] * K[0][1]
 *   R0[2, 4, 6, 8...] * K[0][2]
 *   R1[0, 2, 4, 6...] * K[1][0]
 *   R1[1, 3, 5, 7...] * K[1][1]
 *   R1[2, 4, 6, 8...] * K[1][2]
 *   R2[0, 2, 4, 6...] * K[2][0]
 *   R2[1, 3, 5, 7...] * K[2][1]
 *   R2[2, 4, 6, 8...] * K[2][2]
 * ------------------------------
 *     Output[0, 1, 2, 3]
 */
template <>
struct DepthwiseConvKernel<3, 2> {
  static void run(const float* inputData,
                  const float* filterData,
                  int inputHeight,
                  int inputWidth,
                  int outputChannels,
                  int outputHeight,
                  int outputWidth,
                  int filterMultiplier,
                  float* outputData) {
    const int steps = outputWidth >> 2;
    const int remain = outputWidth & 3;
    for (int c = 0; c < outputChannels; c++, filterData += 9) {
      // Load the filters
      float32x4_t k[3];
      k[0] = vld1q_f32(filterData);
      k[1] = vld1q_f32(filterData + 3);
      k[2] = vld1q_f32(filterData + 6);
      k[0] = vsetq_lane_f32(0.f, k[0], 3);
      k[1] = vsetq_lane_f32(0.f, k[1], 3);
      k[2] = vsetq_lane_f32(0.f, k[2], 3);

      const float* start =
          inputData + (c / filterMultiplier) * (inputHeight * inputWidth);
      float32x4_t input[3][3];
      for (int h = 0; h < outputHeight; h++) {
        const float* r0 = start + 2 * h * inputWidth;
        const float* r1 = start + (2 * h + 1) * inputWidth;
        const float* r2 = start + (2 * h + 2) * inputWidth;
        for (int s = 0; s < steps; s++) {
          // Load the inputs
          float32x4_t data1;
          float32x4x2_t data2;

          data2 = vld2q_f32(r0);
          input[0][0] = data2.val[0];
          input[0][1] = data2.val[1];
          data1 = vld1q_f32(r0 + 8);
          input[0][2] = vextq_f32(data2.val[0], data1, 1);

          data2 = vld2q_f32(r1);
          input[1][0] = data2.val[0];
          input[1][1] = data2.val[1];
          data1 = vld1q_f32(r1 + 8);
          input[1][2] = vextq_f32(data2.val[0], data1, 1);

          data2 = vld2q_f32(r2);
          input[2][0] = data2.val[0];
          input[2][1] = data2.val[1];
          data1 = vld1q_f32(r2 + 8);
          input[2][2] = vextq_f32(data2.val[0], data1, 1);

          float32x4_t tmp1 = vdupq_n_f32(0.f);
          float32x4_t tmp2 = vdupq_n_f32(0.f);
          tmp1 = vmlaq_laneq_f32(tmp1, input[0][0], k[0], 0);
          tmp2 = vmlaq_laneq_f32(tmp2, input[0][1], k[0], 1);
          tmp1 = vmlaq_laneq_f32(tmp1, input[0][2], k[0], 2);
          tmp2 = vmlaq_laneq_f32(tmp2, input[1][0], k[1], 0);
          tmp1 = vmlaq_laneq_f32(tmp1, input[1][1], k[1], 1);
          tmp2 = vmlaq_laneq_f32(tmp2, input[1][2], k[1], 2);
          tmp1 = vmlaq_laneq_f32(tmp1, input[2][0], k[2], 0);
          tmp2 = vmlaq_laneq_f32(tmp2, input[2][1], k[2], 1);
          tmp1 = vmlaq_laneq_f32(tmp1, input[2][2], k[2], 2);
          tmp1 = vaddq_f32(tmp1, tmp2);

          vst1q_f32(outputData, tmp1);
          r0 += 8;
          r1 += 8;
          r2 += 8;
          outputData += 4;
        }

        for (int r = 0; r < remain; r++) {
          float32x4_t i0 = vld1q_f32(r0);
          float32x4_t i1 = vld1q_f32(r1);
          float32x4_t i2 = vld1q_f32(r2);
          *outputData = conv3x3(i0, i1, i2, k[0], k[1], k[2]);
          r0 += 2;
          r1 += 2;
          r2 += 2;
          outputData++;
        }
      }
    }
  }
};

/**
 * Each step calculates four elements of the output.
 */
template <>
struct DepthwiseConvKernel<4, 1> {
  static void run(const float* inputData,
                  const float* filterData,
                  int inputHeight,
                  int inputWidth,
                  int outputChannels,
                  int outputHeight,
                  int outputWidth,
                  int filterMultiplier,
                  float* outputData) {
    const int steps = outputWidth >> 2;
    const int remain = outputWidth & 3;
    for (int c = 0; c < outputChannels; c++, filterData += 16) {
      // Load the filters
      float32x4_t k[4];
      k[0] = vld1q_f32(filterData);
      k[1] = vld1q_f32(filterData + 4);
      k[2] = vld1q_f32(filterData + 8);
      k[3] = vld1q_f32(filterData + 12);

      const float* r0 =
          inputData + (c / filterMultiplier) * (inputHeight * inputWidth);
      const float* r1 = r0 + inputWidth;
      const float* r2 = r0 + inputWidth * 2;
      const float* r3 = r0 + inputWidth * 3;
      float32x4_t input[4][4];
      for (int h = 0; h < outputHeight; h++) {
        for (int s = 0; s < steps; s++) {
          // Load the inputs
          float32x4_t tmp;
          input[0][0] = vld1q_f32(r0);
          tmp = vld1q_f32(r0 + 4);
          input[0][1] = vextq_f32(input[0][0], tmp, 1);
          input[0][2] = vextq_f32(input[0][0], tmp, 2);
          input[0][3] = vextq_f32(input[0][0], tmp, 3);

          input[1][0] = vld1q_f32(r1);
          tmp = vld1q_f32(r1 + 4);
          input[1][1] = vextq_f32(input[1][0], tmp, 1);
          input[1][2] = vextq_f32(input[1][0], tmp, 2);
          input[1][3] = vextq_f32(input[1][0], tmp, 3);

          input[2][0] = vld1q_f32(r2);
          tmp = vld1q_f32(r2 + 4);
          input[2][1] = vextq_f32(input[2][0], tmp, 1);
          input[2][2] = vextq_f32(input[2][0], tmp, 2);
          input[2][3] = vextq_f32(input[2][0], tmp, 3);

          input[3][0] = vld1q_f32(r3);
          tmp = vld1q_f32(r3 + 4);
          input[3][1] = vextq_f32(input[3][0], tmp, 1);
          input[3][2] = vextq_f32(input[3][0], tmp, 2);
          input[3][3] = vextq_f32(input[3][0], tmp, 3);

          float32x4_t tmp1 = vdupq_n_f32(0.f);
          float32x4_t tmp2 = vdupq_n_f32(0.f);
          tmp1 = vmlaq_laneq_f32(tmp1, input[0][0], k[0], 0);
          tmp2 = vmlaq_laneq_f32(tmp2, input[0][1], k[0], 1);
          tmp1 = vmlaq_laneq_f32(tmp1, input[0][2], k[0], 2);
          tmp2 = vmlaq_laneq_f32(tmp2, input[0][3], k[0], 3);
          tmp1 = vmlaq_laneq_f32(tmp1, input[1][0], k[1], 0);
          tmp2 = vmlaq_laneq_f32(tmp2, input[1][1], k[1], 1);
          tmp1 = vmlaq_laneq_f32(tmp1, input[1][2], k[1], 2);
          tmp2 = vmlaq_laneq_f32(tmp2, input[1][3], k[1], 3);
          tmp1 = vmlaq_laneq_f32(tmp1, input[2][0], k[2], 0);
          tmp2 = vmlaq_laneq_f32(tmp2, input[2][1], k[2], 1);
          tmp1 = vmlaq_laneq_f32(tmp1, input[2][2], k[2], 2);
          tmp2 = vmlaq_laneq_f32(tmp2, input[2][3], k[2], 3);
          tmp1 = vmlaq_laneq_f32(tmp1, input[3][0], k[3], 0);
          tmp2 = vmlaq_laneq_f32(tmp2, input[3][1], k[3], 1);
          tmp1 = vmlaq_laneq_f32(tmp1, input[3][2], k[3], 2);
          tmp2 = vmlaq_laneq_f32(tmp2, input[3][3], k[3], 3);
          tmp1 = vaddq_f32(tmp1, tmp2);

          vst1q_f32(outputData, tmp1);
          r0 += 4;
          r1 += 4;
          r2 += 4;
          r3 += 4;
          outputData += 4;
        }

        for (int r = 0; r < remain; r++) {
          float32x4_t i0 = vld1q_f32(r0);
          float32x4_t i1 = vld1q_f32(r1);
          float32x4_t i2 = vld1q_f32(r2);
          float32x4_t i3 = vld1q_f32(r3);
          *outputData = conv4x4(i0, i1, i2, i3, k[0], k[1], k[2], k[3]);
          r0++;
          r1++;
          r2++;
          r3++;
          outputData++;
        }

        r0 += 3;
        r1 += 3;
        r2 += 3;
        r3 += 3;
      }
    }
  }
};

/**
 * Each step calculates four elements of the output.
 */
template <>
struct DepthwiseConvKernel<4, 2> {
  static void run(const float* inputData,
                  const float* filterData,
                  int inputHeight,
                  int inputWidth,
                  int outputChannels,
                  int outputHeight,
                  int outputWidth,
                  int filterMultiplier,
                  float* outputData) {
    const int steps = outputWidth >> 2;
    const int remain = outputWidth & 3;
    for (int c = 0; c < outputChannels; c++, filterData += 16) {
      // Load the filters
      float32x4_t k[4];
      k[0] = vld1q_f32(filterData);
      k[1] = vld1q_f32(filterData + 4);
      k[2] = vld1q_f32(filterData + 8);
      k[3] = vld1q_f32(filterData + 12);

      const float* start =
          inputData + (c / filterMultiplier) * (inputHeight * inputWidth);
      float32x4_t input[4][4];
      for (int h = 0; h < outputHeight; h++) {
        const float* r0 = start + 2 * h * inputWidth;
        const float* r1 = start + (2 * h + 1) * inputWidth;
        const float* r2 = start + (2 * h + 2) * inputWidth;
        const float* r3 = start + (2 * h + 3) * inputWidth;
        for (int s = 0; s < steps; s++) {
          // Load the inputs
          float32x4x2_t data1;
          float32x4x2_t data2;

          data1 = vld2q_f32(r0);
          data2 = vld2q_f32(r0 + 8);
          input[0][0] = data1.val[0];
          input[0][1] = data1.val[1];
          input[0][2] = vextq_f32(data1.val[0], data2.val[0], 1);
          input[0][3] = vextq_f32(data1.val[1], data2.val[1], 1);

          data1 = vld2q_f32(r1);
          data2 = vld2q_f32(r1 + 8);
          input[1][0] = data1.val[0];
          input[1][1] = data1.val[1];
          input[1][2] = vextq_f32(data1.val[0], data2.val[0], 1);
          input[1][3] = vextq_f32(data1.val[1], data2.val[1], 1);

          data1 = vld2q_f32(r2);
          data2 = vld2q_f32(r2 + 8);
          input[2][0] = data1.val[0];
          input[2][1] = data1.val[1];
          input[2][2] = vextq_f32(data1.val[0], data2.val[0], 1);
          input[2][3] = vextq_f32(data1.val[1], data2.val[1], 1);

          data1 = vld2q_f32(r3);
          data2 = vld2q_f32(r3 + 8);
          input[3][0] = data1.val[0];
          input[3][1] = data1.val[1];
          input[3][2] = vextq_f32(data1.val[0], data2.val[0], 1);
          input[3][3] = vextq_f32(data1.val[1], data2.val[1], 1);

          float32x4_t tmp1 = vdupq_n_f32(0.f);
          float32x4_t tmp2 = vdupq_n_f32(0.f);
          tmp1 = vmlaq_laneq_f32(tmp1, input[0][0], k[0], 0);
          tmp2 = vmlaq_laneq_f32(tmp2, input[0][1], k[0], 1);
          tmp1 = vmlaq_laneq_f32(tmp1, input[0][2], k[0], 2);
          tmp2 = vmlaq_laneq_f32(tmp2, input[0][3], k[0], 3);
          tmp1 = vmlaq_laneq_f32(tmp1, input[1][0], k[1], 0);
          tmp2 = vmlaq_laneq_f32(tmp2, input[1][1], k[1], 1);
          tmp1 = vmlaq_laneq_f32(tmp1, input[1][2], k[1], 2);
          tmp2 = vmlaq_laneq_f32(tmp2, input[1][3], k[1], 3);
          tmp1 = vmlaq_laneq_f32(tmp1, input[2][0], k[2], 0);
          tmp2 = vmlaq_laneq_f32(tmp2, input[2][1], k[2], 1);
          tmp1 = vmlaq_laneq_f32(tmp1, input[2][2], k[2], 2);
          tmp2 = vmlaq_laneq_f32(tmp2, input[2][3], k[2], 3);
          tmp1 = vmlaq_laneq_f32(tmp1, input[3][0], k[3], 0);
          tmp2 = vmlaq_laneq_f32(tmp2, input[3][1], k[3], 1);
          tmp1 = vmlaq_laneq_f32(tmp1, input[3][2], k[3], 2);
          tmp2 = vmlaq_laneq_f32(tmp2, input[3][3], k[3], 3);
          tmp1 = vaddq_f32(tmp1, tmp2);

          vst1q_f32(outputData, tmp1);
          r0 += 8;
          r1 += 8;
          r2 += 8;
          r3 += 8;
          outputData += 4;
        }

        for (int r = 0; r < remain; r++) {
          float32x4_t i0 = vld1q_f32(r0);
          float32x4_t i1 = vld1q_f32(r1);
          float32x4_t i2 = vld1q_f32(r2);
          float32x4_t i3 = vld1q_f32(r3);
          *outputData = conv4x4(i0, i1, i2, i3, k[0], k[1], k[2], k[3]);
          r0 += 2;
          r1 += 2;
          r2 += 2;
          r3 += 2;
          outputData++;
        }
      }
    }
  }
};

template <class T>
struct Padding {
  static void run(const T* src,
                  T* dest,
                  int channels,
                  int inputHeight,
                  int inputWidth,
                  int paddingHeight,
                  int paddingWidth) {
    const int destWidth = inputWidth + 2 * paddingWidth;
    for (int c = 0; c < channels; c++) {
      if (paddingHeight > 0) {
        memset(dest, 0, destWidth * paddingHeight * sizeof(T));
        dest += destWidth * paddingHeight;
      }

      for (int i = 0; i < inputHeight; i++) {
        // padding head
        for (int j = 0; j < paddingWidth; j++) {
          *dest++ = T(0);
        }

        memcpy(dest, src, inputWidth * sizeof(T));
        dest += inputWidth;
        src += inputWidth;

        // padding tail
        for (int j = 0; j < paddingWidth; j++) {
          *dest++ = T(0);
        }
      }

      if (paddingHeight > 0) {
        memset(dest, 0, destWidth * paddingHeight * sizeof(T));
        dest += destWidth * paddingHeight;
      }
    }
  }
};

#if defined(__ARM_NEON__) || defined(__ARM_NEON)
template <>
struct Padding<float> {
  static void run(const float* src,
                  float* dest,
                  int channels,
                  int inputHeight,
                  int inputWidth,
                  int paddingHeight,
                  int paddingWidth) {
    const int destWidth = inputWidth + 2 * paddingWidth;
    for (int c = 0; c < channels; c++) {
      if (paddingHeight > 0) {
        memset(dest, 0, destWidth * paddingHeight * sizeof(float));
        dest += destWidth * paddingHeight;
      }

      for (int i = 0; i < inputHeight; i++) {
        // padding head
        for (int j = 0; j < paddingWidth; j++) {
          *dest++ = float(0);
        }

        int step = inputWidth >> 2;
        int remain = inputWidth & 3;
        for (int s = 0; s < step; s++) {
          float32x4_t s0 = vld1q_f32(src);
          vst1q_f32(dest, s0);
          src += 4;
          dest += 4;
        }
        for (int r = 0; r < remain; r++) {
          *dest++ = *src++;
        }

        // padding tail
        for (int j = 0; j < paddingWidth; j++) {
          *dest++ = float(0);
        }
      }

      if (paddingHeight > 0) {
        memset(dest, 0, destWidth * paddingHeight * sizeof(float));
        dest += destWidth * paddingHeight;
      }
    }
  }
};

#endif

#endif

}  // namespace neon
}  // namespace paddle
