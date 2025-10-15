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


class RooflineBench_GFX950(RooflineBench_Base):
    def __init__(self) -> None:
        self.MX_DATAFORMATS = self.ENUM_MX_DATAFORMATS
        self.ARCH_SPECS = self.ENUM_ARCH_SPECS

        # Unsupported = ""
        self.SUPPORTED_DT = [
            "HBM",
            "MALL",
            "L2",
            "L1",
            "LDS",
            "FP8",
            "FP16",
            "BF16",
            "FP32",
            "FP64",
            "I8",
            "I32",
            "I64",
            "MFMA-F4",
            "MFMA-F6",
            "MFMA-F8",
            "MFMA-F16",
            "MFMA-BF16",
            "MFMA-F32",
            "MFMA-F64",
            "MFMA-I8",
        ]

    # TODO: check numbers
    L2_CACHE_SIZE = 8 * 1024 * 1024
    L1_CACHE_SIZE = 16 * 1024
    LDS_SIZE = 65536

    # MFMA f8f6f4 kernels take dataformat enum value
    class ENUM_MX_DATAFORMATS(IntEnum):
        FP8 = 0
        BF8 = 1
        FP6 = 2
        BF6 = 3
        FP4 = 4

    # TODO: check numbers THESE DON'T MATCH THE CACHE SIZE ABOVE
    # I THINK LDS SIZE IS DIFFERENT FOR GFX950 CHECK WHITEPAPER
    class ENUM_ARCH_SPECS(IntEnum):
        L1 = (32 * 1024,)
        L2 = (4 * 1024 * 1024,)
        MALL = (64 * 1024 * 1024,)
        LDS = (64 * 1024,)
        CU = 256

    # ----------------------------------------
    # GFX950 Templated Kernels
    # ----------------------------------------

    sourceMFMAi8 = """
    __global__ void mfma_i8(int iter, float *dummy)
    {
        using int32_16vec = __attribute__((__vector_size__(16 * sizeof(int)))) int;

        // Output: 16 I32 registers
        int32_16vec result = {0};

        // Input: 2 I32 registers
        // builting mfma expects I64 input
        long a =  threadIdx.x;

        // CDNA3: v_mfma_i32_32x32x16_i8 ops: 32x32x16x2 = 32768
        for(int i = 0; i < iter; ++i)
        {
            result = __builtin_amdgcn_mfma_i32_32x32x16_i8(a, a, result, 0, 0, 0);
        }

        if (result[0] != 2*result[0])
        {
            dummy[0] = result[0];
        }
    }
    """

    # TODO: need to figure out how to pass in datatype format
    sourceMFMAf8f6f4 = """
    /* Datatypes available for scale mfma f8f6f4 builtin
    * 0 = fp8
    * 1 = bf8
    * 2 = fp6
    * 3 = bf6
    * 4 = fp4
    */
    __global__ void mfma_f8f6f4(int iter, float *dummy, MX_DATAFORMATS datatype)
    {
        using int32_8vec = __attribute__((__vector_size__(8 * sizeof(int)))) int;
        using f32_16vec = __attribute__((__vector_size__(16 * sizeof(float)))) float;

        // Input: 8 i32 registers
        int32_8vec a;
        a[0] = a[1] = a[2] = a[3] = a[4] = a[5] = a[6] = a[7] = threadIdx.x;

        // Output: 16 F32 registers
        f32_16vec result = {0};

        // CDNA4: v_mfma_f32_32x32x64_f8f6f4    ops: 32x32x64x2 = 131072
        switch (datatype)
        {
            case 1: // bf8 x bf8
                for(int i = 0; i < iter; ++i)
                {
                    result = __builtin_amdgcn_mfma_scale_f32_32x32x64_f8f6f4\
                        (a, a, result, 1, 1, 0, 0, 0, 0);
                }
                break;
            case 2: // fp6 x fp6
                for(int i = 0; i < iter; ++i)
                {
                    result = __builtin_amdgcn_mfma_scale_f32_32x32x64_f8f6f4\
                        (a, a, result, 2, 2, 0, 0, 0, 0);
                }
                break;
            case 3: // bf6 x bf6
                for(int i = 0; i < iter; ++i)
                {
                    result = __builtin_amdgcn_mfma_scale_f32_32x32x64_f8f6f4\
                        (a, a, result, 3, 3, 0, 0, 0, 0);
                }
                break;
            case 4: // fp4 x fp4
                for(int i = 0; i < iter; ++i)
                {
                    result = __builtin_amdgcn_mfma_scale_f32_32x32x64_f8f6f4\
                        (a, a, result, 4, 4, 0, 0, 0, 0);
                }
                break;
        default: // fp8 x fp8
            for(int i = 0; i < iter; ++i)
            {
                result = __builtin_amdgcn_mfma_scale_f32_32x32x64_f8f6f4\
                    (a, a, result, 0, 0, 0, 0, 0, 0);
            }
            break;
        }

        if (result[0] != 2*result[0])
        {
            dummy[0] = result[0];
        }
    }
    """

    sourceMFMAf8 = """
    __global__ void mfma_f8(int iter, float *dummy)
    {
        using f32_16vec = __attribute__((__vector_size__(16 * sizeof(float)))) float;

        // MI300 series only - note gfx940/gfx941/gfx942 only uses fnuz f8
        // Input: 2 F32 registers
        // builtin mfma expects double input
        double a =  threadIdx.x;

        // Output: 16 F32 registers
        f32_16vec result = {0};

        // CDNA3: v_mfma_f32_32x32x16_fp8_fp8 ops: 32x32x16x2 = 32768
        for(int i = 0; i < iter; ++i)
        {
            result = __builtin_amdgcn_mfma_f32_32x32x16_fp8_fp8(a, a, result, 0, 0, 0);
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
        using bf16_4vec = __attribute__((__vector_size__(2 * sizeof(__2i16))))  short;
        using f32_16vec = __attribute__((__vector_size__(16 * sizeof(float)))) float;

        // Output: 16 F32 registers
        f32_16vec result = {0};

        // Input: 2 F32 registers
        // builting mfma expects 4 short registers
        bf16_4vec a;
        a[3] = a[2] = a[1] = a[0]= threadIdx.x;

        // CDNA3: v_mfma_f32_32x32x8_bf16 ops: 32x32x8x2 = 16384
        for(int i = 0; i < iter; ++i)
        {
            result = __builtin_amdgcn_mfma_f32_32x32x8bf16_1k(a, a, result, 0, 0, 0);
        }

        if (result[0] != 2*result[0])
        {
            dummy[0] = result[0];
        }
    }
    """
