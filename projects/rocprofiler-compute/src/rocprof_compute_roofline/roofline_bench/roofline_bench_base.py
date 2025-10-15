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

import ctypes
from enum import IntEnum
from math import sqrt

from pyhip import hip

from utils.logger import console_log


class RooflineBench_Base:
    def __init__(self) -> None:
        self.NUM_OPS = self.ENUM_NUM_OPS
        # Overwrite supported datatypes per architecture
        self.SUPPORTED_DT = []

        # TODO: pyhip raises errors already, might not need this
        # def HIP_ASSERT(x) { auto ret = (x); if(ret != hipSuccess) \
        # { fprintf(stderr, "%s:%d :: HIP error : %s\n", __FILE__, __LINE__, \
        # hipGetErrorString(ret));  throw std::runtime_error("hip_error"); }}

        self.SIMDS_PER_CU = 4

        # Defaults for benchmarks
        self.DEFAULT_WORKGROUP_SIZE = 256
        self.DEFAULT_WORKGROUPS = 8192
        self.DEFAULT_THREADS = self.DEFAULT_WORKGROUP_SIZE * self.DEFAULT_WORKGROUPS
        self.DEFAULT_NUM_EXPERIMENTS = 100
        self.DEFAULT_NUM_ITERS = 10
        self.DEFAULT_DATASET_SIZE = 512 * 1024 * 1024

        # Dictionary to store benchmark values
        self.benchmarkDeviceResults: dict[str, int] = {
            "device": -1,
            "HBMBw": 0,
            "HBMBwLow": 0,
            "HBMBwHigh": 0,
            "MALLBw": 0,
            "MALLBwLow": 0,
            "MALLBwHigh": 0,
            "L2Bw": 0,
            "L2BwLow": 0,
            "L2BwHigh": 0,
            "L1Bw": 0,
            "L1BwLow": 0,
            "L1BwHigh": 0,
            "LDSBw": 0,
            "LDSBwLow": 0,
            "LDSBwHigh": 0,
            "FP8Flops": 0,
            "FP8FlopsLow": 0,
            "FP8FlopsHigh": 0,
            "FP16Flops": 0,
            "FP16FlopsLow": 0,
            "FP16FlopsHigh": 0,
            "BF16Flops": 0,
            "BF16FlopsLow": 0,
            "BF16FlopsHigh": 0,
            "FP32Flops": 0,
            "FP32FlopsLow": 0,
            "FP32FlopsHigh": 0,
            "FP64Flops": 0,
            "FP64FlopsLow": 0,
            "FP64FlopsHigh": 0,
            "I8Ops": 0,
            "I8OpsLow": 0,
            "I8OpsHigh": 0,
            "I32Ops": 0,
            "I32OpsLow": 0,
            "I32OpsHigh": 0,
            "I64Ops": 0,
            "I64OpsLow": 0,
            "I64OpsHigh": 0,
            "MFMAF4Flops": 0,
            "MFMAF4FlopsLow": 0,
            "MFMAF4FlopsHigh": 0,
            "MFMAF6Flops": 0,
            "MFMAF6FlopsLow": 0,
            "MFMAF6FlopsHigh": 0,
            "MFMAF8Flops": 0,
            "MFMAF8FlopsLow": 0,
            "MFMAF8FlopsHigh": 0,
            "MFMAF16Flops": 0,
            "MFMAF16FlopsLow": 0,
            "MFMAF16FlopsHigh": 0,
            "MFMABF16Flops": 0,
            "MFMABF16FlopsLow": 0,
            "MFMABF16FlopsHigh": 0,
            "MFMAF32Flops": 0,
            "MFMAF32FlopsLow": 0,
            "MFMAF32FlopsHigh": 0,
            "MFMAF64Flops": 0,
            "MFMAF64FlopsLow": 0,
            "MFMAF64FlopsHigh": 0,
            "MFMAI8Ops": 0,
            "MFMAI8OpsLow": 0,
            "MFMAI8OpsHigh": 0,
        }

        # List of benchmarkDeviceResults, per device
        self.benchmarkResultsList: list[dict] = []

    # Total number of OPS (static) for the respective builtin kernel used to benchmark
    class ENUM_NUM_OPS(IntEnum):
        MFMA_F4_OPS = (131072,)
        MFMA_F6_OPS = (131072,)
        MFMA_F8_OPS = (32768,)
        MFMA_F16_OPS = (16384,)
        MFMA_F32_OPS = (4096,)
        MFMA_F64_OPS = (2048,)
        MFMA_I8_OPS = (32768,)
        MFMA_BF16_OPS = 16384

    # ----------------------------------------
    # Utils
    # ----------------------------------------

    # Progress bar visual output
    def showProgress(self, percentage: float) -> None:
        PBSTR = "||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||"
        PBWIDTH = 60
        val = int(percentage * 100)
        lpad = int(percentage * PBWIDTH)
        rpad = PBWIDTH - lpad
        console_log("bench", f"\r   {val}% [{PBSTR[:lpad]}{' ' * rpad}]")

    # Return device architecture
    def deviceArch(self, device_id: int) -> str:
        device_properties = hip.hipGetDeviceProperties(device_id)
        return str(device_properties.gcnArchName).split(":")[0]

    # Calculate statistics after kernel run
    def stats(self, samples: list[float], entries: int) -> tuple[float, float, float]:
        mean_val, stdev_val, conf_val = 0.0

        for i in range(entries):
            mean_val += samples[i]

        mean_val = mean_val / entries

        for i in range(entries):
            stdev_val += (samples[i] - mean_val) * (samples[i] - mean_val)

        stdev_val = sqrt(stdev_val / entries)

        conf_val = 1.960 * stdev_val / sqrt(entries)

        # TODO: check return values, we can't use pointers like before
        return mean_val, stdev_val, conf_val

    # ----------------------------------------
    # HIP Events
    # ----------------------------------------

    # Start hip capture
    def initHipEvents(self) -> tuple[ctypes.c_void_p, ctypes.c_void_p]:
        start = hip.hipEventCreate()
        stop = hip.hipEventCreate()
        hip.hipEventRecord(start)
        return start, stop

    # Stop hip capture
    def stopHipEvents(self, start: ctypes.c_void_p, stop: ctypes.c_void_p) -> float:
        hip.hipEventRecord(stop)
        hip.hipEventSynchronize(stop)
        eventMs = hip.hipEventElapsedTime(start, stop)
        hip.hipEventDestroy(start)
        hip.hipEventDestroy(stop)
        return eventMs

    # ----------------------------------------
    # Base Templated Kernels
    # ----------------------------------------

    # Memory Bandwidth Benchmarks

    sourceCacheBw = """
    template <typename T, int cacheSize, int workgroup_size>
    __global__ void Cache_bw(const T *memBlock, T *dummy, int numIter)
    {
    const int thread_id = threadIdx.x;
    constexpr int cache_count = cacheSize / sizeof(T);

    T sink;

    sink = 0;
    for (int iter = 0; iter < numIter; ++iter)
    {
    #pragma unroll 32
        for (int i = 0; i < cache_count; i += workgroup_size)
        {
        // if the size of the memory block is small (e.g., the size
        // of L1), then we need a slightly more complicated index
        // calculation. Otherwise, the compiler holds all the loads
        // in the inner loop in registers upon the first pass of the
        // outer loop, and it doesn't do the loads upon subsequent
        // passes of the outer loop.
        // OTOH, if the size of the memory block is larger (such as L2
        // size), experimentation showed that the overhead of the more
        // complicated index calculation has a noticeable effect on BW,
        // so we use a simpler index expression instead. This works since
        // for larger memory blocks, the compiler cannot hold the loads
        // of the inner loop in registers anymore, as it can with L1-sized
        // buffers.
        if constexpr (cache_count / workgroup_size <= 32)
        {
            sink += memBlock[(thread_id + i + iter) % cache_count];
        }
        else
        {
            sink += memBlock[thread_id + i];
        }
        }
    }

    dummy[thread_id] = sink;
    }
    """

    sourceHbmBw = """
    template<typename T>
    __global__ void HBM_bw(T *dst, const T *src)
    {
        const uint32_t gid = hipBlockDim_x * hipBlockIdx_x + hipThreadIdx_x;
        const uint32_t tid = hipThreadIdx_x;

        dst[gid] = src[gid];
    }
    """

    sourceLdsBw = """
    __global__ void LDS_bw(int numIter, float *dummy)
    {
        const uint32_t tid = threadIdx.x;
        __shared__ uint8_t shmem[64];


        if (tid == 0)
        {
            #pragma unroll
            for (int i=0;i<63;i++)
                shmem[i] = i+1;

            shmem[63] = 0;
        }

        __syncthreads();

        uint32_t index = tid;
        #pragma unroll 64
        for(uint32_t iter = 0; iter < numIter; iter++)
            index = shmem[index];

        dummy[tid] = (float )index;
    }
    """

    # VALU Benchmarks

    sourceFlopsBenchmark = """
    template<typename T, int nFMA>
    __global__ void flops_benchmark(T *buf, uint32_t nSize)
    {
        const uint32_t gid = hipBlockDim_x * hipBlockIdx_x + hipThreadIdx_x;
        const uint32_t nThreads  = gridDim.x * blockDim.x;
        const uint32_t nEntriesPerThread = (uint32_t) nSize / nThreads;
        const uint32_t maxOffset = nEntriesPerThread * nThreads;

        T *ptr;
        const T y = (T) 1.0;

        ptr = &buf[gid];
        T x = (T) 2.0;

        for(uint32_t offset=0; offset < maxOffset; offset += nThreads)
        {
            for(int j=0; j<nFMA; j++)
            {
                x = ptr[offset] * x + y;
            }
        }

        ptr[0] = -x;
    }
    """

    # MFMA Benchmarks

    sourceMFMAf16 = """
    __global__ void mfma_f16(int iter, float *dummy)
    {
        using f32_16vec = __attribute__((__vector_size__(16 * sizeof(float)))) float;
        using f16_2vec = __attribute__((__vector_size__(2 * sizeof(__2f16))))  float;

        // Input: 2 F32 registers
        f16_2vec a;
        a[1] = a[0] = threadIdx.x;

        //Output: 16 F32 registers
        f32_16vec result = {0};

        // CDNA2: v_mfma_f32_32x32x8f16 ops: 32x32x8x2 = 16384
        // CDNA3: v_mfma_f32_32x32x8_f16
        for(int i = 0; i < iter; ++i)
        {
            result = __builtin_amdgcn_mfma_f32_32x32x8f16(a, a, result, 0, 0, 0);
        }

        if (result[0] != 2*result[0])
        {
            dummy[0] = result[0];
        }

    }
    """

    sourceMFMAf32 = """
    __global__ void mfma_f32(int iter, float *dummy)
    {
        using f32_16vec = __attribute__((__vector_size__(16 * sizeof(float)))) float;

        // Input: 1 F32 register
        float a =  threadIdx.x;

        // Output: 16 F32 registers
        f32_16vec result = {0};

        // CDNA2: v_mfma_f32_32x32x2f32 ops: 32x32x2x2 = 4096
        // CDNA3: v_mfma_f32_32x32x2_f32
        for(int i = 0; i < iter; ++i)
        {
            result = __builtin_amdgcn_mfma_f32_32x32x2f32(a, a, result, 0, 0, 0);
        }

        if (result[0] != 2*result[0])
        {
            dummy[0] = result[0];
        }

    }
    """

    sourceMFMAf64 = """
    __global__ void mfma_f64(int iter, float *dummy)
    {
        using f64_4vec = __attribute__((__vector_size__(4 * sizeof(double)))) double;

        // Input: 1 F64 register
        double a =  threadIdx.x;

        // Output: 4 F64 registers
        f64_4vec result = {0};

        // CDNA2: v_mfma_f64_16x16x4f64 ops: 16x16x4x2 = 2048
        // CDNA3: v_mfma_f64_16x16x4_f64
        for(int i = 0; i < iter; ++i)
        {
            result = __builtin_amdgcn_mfma_f64_16x16x4f64(a, a, result, 0, 0, 0);
        }

        if (result[0] != 2*result[0])
        {
            dummy[0] = result[0];
        }
    }
    """

    # Override if applicable, these kernels or datatypes are unique to the archs
    sourceMFMAi8 = """"""
    sourceMFMAf8f6f4 = """"""
    sourceMFMAf8 = """"""
    sourceMFMAbf16 = """"""

    # ----------------------------------------
    # Base Benchmarking Function
    # ----------------------------------------

    def benchmark(self, roofPath: str, device: int) -> None:
        devID = device
        # csvFile = roofPath

        # workgroupSize = self.DEFAULT_WORKGROUP_SIZE
        # numWorkgroups = self.DEFAULT_WORKGROUPS
        # datasetEntries = self.DEFAULT_DATASET_SIZE
        # numIters = self.DEFAULT_NUM_ITERS
        # numExperiments = self.DEFAULT_NUM_EXPERIMENTS
        # numBenchmarks = 11

        # totalThreads = numWorkgroups * workgroupSize

        numGpuDevices = hip.hipGetDeviceCount
        if (devID >= 0) and (devID < numGpuDevices):
            gcnArch = self.deviceArch(devID)
            if gcnArch not in self.SUPPORTED_DT:
                print(
                    f'Unsupported device architecture "{gcnArch}" \
                       will be skipped\n'
                )
        print(f"Total detected GPU devices: {numGpuDevices}\n")
