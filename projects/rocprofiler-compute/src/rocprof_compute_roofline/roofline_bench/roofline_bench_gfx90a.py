##############################################################################
# MIT License
#
# Copyright (c) 2021 - 2025 Advanced Micro Devices, Inc. All Rights Reserved.
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.

##############################################################################

from enum import IntEnum

from rocprof_compute_roofline.roofline_bench.roofline_bench_base import (
    RooflineBench_Base,
)


class RooflineBench_GFX90A(RooflineBench_Base):
    def __init__(self) -> None:
        self.NUM_OPS = self.ENUM_NUM_OPS
        self.ARCH_SPECS = self.ENUM_ARCH_SPECS

        # Unsupported = "MALL", "FP8", "MFMA-F4", "MFMA-F6", "MFMA-F8"
        self.SUPPORTED_DT = [
            "HBM",
            "L2",
            "L1",
            "LDS",
            "FP16",
            "BF16",
            "FP32",
            "FP64",
            "I8",
            "I32",
            "I64",
            "MFMA-F16",
            "MFMA-BF16",
            "MFMA-F32",
            "MFMA-F64",
            "MFMA-I8",
        ]

    L2_CACHE_SIZE = 8 * 1024 * 1024
    L1_CACHE_SIZE = 16 * 1024
    LDS_SIZE = 65536

    # Total number of OPS (static) for the respective builtin kernel used to benchmark
    # GFX90A's I8 and BF16 kernels have different OPS count \
    # (opposed to other architectures), overwrite for this arch class
    class ENUM_NUM_OPS(IntEnum):
        MFMA_F4_OPS = (131072,)
        MFMA_F6_OPS = (131072,)
        MFMA_F8_OPS = (32768,)
        MFMA_F16_OPS = (16384,)
        MFMA_F32_OPS = (4096,)
        MFMA_F64_OPS = (2048,)
        MFMA_I8_OPS = (16384,)
        MFMA_BF16_OPS = 8192

    class ENUM_ARCH_SPECS(IntEnum):
        L1 = (16 * 1024,)
        L2 = (8 * 1024 * 1024,)
        MALL = (0,)
        LDS = (64 * 1024,)
        CU = 104

    # ----------------------------------------
    # GFX90A Templated Kernels
    # -----------------------------------------

    sourceMFMAi8 = """
    __global__ void mfma_i8(int iter, float *dummy)
    {
        using int32_16vec = __attribute__((__vector_size__(16 * sizeof(int)))) int;

        // Output: 16 I32 registers
        int32_16vec result = {0};

        // Input: 1 I32 register
        int a = threadIdx.x;

        // CDNA1/2: v_mfma_i32_32x32x8i8 ops: 32x32x8x2 = 16384
        for(int i = 0; i < iter; ++i)
        {
            result = __builtin_amdgcn_mfma_i32_32x32x8i8(a, a, result, 0, 0, 0);
        }

        if (result[0] != 2*result[0])
        {
            dummy[0] = result[0];
        }
    }
    """

    sourceMFMAbf16 = """
    __global__ void mfma_bf16(int iter, float *dummy)
    {
        using bf16_2vec = __attribute__((__vector_size__(1 * sizeof(__2i16))))  short;
        using f32_16vec = __attribute__((__vector_size__(16 * sizeof(float)))) float;

        // Output: 16 F32 registers
        f32_16vec result = {0};

        // Input: 1 F32 register
        // builtin mfma expects 2 short registers
        bf16_2vec a;
        a[1] = a[0]= threadIdx.x;

        // CDNA1/2: v_mfma_f32_32x32x4bf16 ops: 32x32x4x2 = 8192
        for(int i = 0; i < iter; ++i)
        {
            result = __builtin_amdgcn_mfma_f32_32x32x4bf16(a, a, result, 0, 0, 0);
        }

        if (result[0] != 2*result[0])
        {
            dummy[0] = result[0];
        }
    }
    """
