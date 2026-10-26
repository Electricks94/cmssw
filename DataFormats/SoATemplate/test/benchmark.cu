#include <iostream>
#include <cstdint>
#include <cstddef>
#include <cmath>
#include <memory>

#include "DataFormats/SoATemplate/interface/SoALayout.h"

#include "HeterogeneousCore/CUDAUtilities/interface/cudaCheck.h"
#include "HeterogeneousCore/CUDAUtilities/interface/requireDevices.h"

// SoA Definition
GENERATE_SOA_LAYOUT(SoATemplate,
                    SOA_COLUMN(float, c1),
                    SOA_COLUMN(float, c2),
                    SOA_COLUMN(float, c3),
                    SOA_COLUMN(float, c4),
                    SOA_COLUMN(float, c5),
                    SOA_COLUMN(float, c6),
                    SOA_COLUMN(float, c7),
                    SOA_COLUMN(float, c8),
                    SOA_COLUMN(int16_t, c9),
                    SOA_COLUMN(int32_t, c10),
                    SOA_COLUMN(int16_t, c11),
                    SOA_COLUMN(int16_t, c12),
                    SOA_COLUMN(uint16_t, c13),
                    SOA_COLUMN(int32_t, c14))

using SoA = SoATemplate<>;

// Memory bound kernel
template <typename View>
__global__ void memoryBoundKernel(View view, int numElements) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= numElements) return;

    // Load multiple float columns
    float val1 = view[i].c1();
    float val2 = view[i].c2();
    float val3 = view[i].c3();
    float val4 = view[i].c4();
    float val5 = view[i].c5();
    float val6 = view[i].c6();
    float val7 = view[i].c7();
    float val8 = view[i].c8();

    // Low arithmetic intensity transformations (chaining loads/stores)
    view[i].c1() = val1 + val2 * 1.001f;
    view[i].c2() = val2 + val3 * 1.001f;
    view[i].c3() = val3 + val4 * 1.001f;
    view[i].c4() = val4 + val5 * 1.001f;
    view[i].c5() = val5 + val6 * 1.001f;
    view[i].c6() = val6 + val7 * 1.001f;
    view[i].c7() = val7 + val8 * 1.001f;
    view[i].c8() = val8 + val1 * 1.001f;

    // Access non-float columns to verify wide layout integration
    int32_t c10_val = view[i].c10();
    view[i].c10() = c10_val + 1;
}

// compute bound kernel
template <typename View>
__global__ void computeBoundKernel(View view, int numElements) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= numElements) return;

    float x = view[i].c1();
    float y = view[i].c2();

    // Heavy arithmetic workload (32 unrolled iterations of transcendental math)
    #pragma unroll
    for (int iter = 0; iter < 32; ++iter) {
        x = __sinf(x) * __cosf(y) + __expf(-x * 0.01f);
        y = __cosf(y) * __sinf(x) + __logf(fabsf(x) + 1.0f);
    }

    view[i].c1() = x;
    view[i].c2() = y;
}

int main() {
    constexpr int numElements = 4 * 1024 * 1024; // 4 Million elements for robust profiling
    constexpr int blockSize = 256;
    int numBlocks = (numElements + blockSize - 1) / blockSize;

    constexpr int warmupIterations = 10;
    constexpr int timedIterations = 100;

    std::cout << "========================================\n";
    std::cout << "       SoA CUDA Benchmark               \n";
    std::cout << "========================================\n";
    std::cout << "Elements    : " << numElements << "\n";
    std::cout << "Block Size  : " << blockSize << "\n";
    std::cout << "Grid Size   : " << numBlocks << "\n";

    // Allocate device buffer using computed size
    size_t bufferSize = SoA::computeDataSize(numElements);
    std::byte* d_buffer = nullptr;
    CUDA_CHECK(cudaMalloc(&d_buffer, bufferSize));

    // Construct SoA and View
    SoA soa(d_buffer, numElements);
    SoA::View view(soa);

    // CUDA Events for timing
    cudaEvent_t start, stop;
    CUDA_CHECK(cudaEventCreate(&start));
    CUDA_CHECK(cudaEventCreate(&stop));

    // --- 1. Benchmark Memory-Bound Kernel ---
    std::cout << "\n[1/2] Running Memory-Bound Kernel..." << std::endl;
    for (int w = 0; w < warmupIterations; ++w) {
        memoryBoundKernel<<<numBlocks, blockSize>>>(view, numElements);
    }
    CUDA_CHECK(cudaDeviceSynchronize());

    CUDA_CHECK(cudaEventRecord(start));
    for (int it = 0; it < timedIterations; ++it) {
        memoryBoundKernel<<<numBlocks, blockSize>>>(view, numElements);
    }
    CUDA_CHECK(cudaEventRecord(stop));
    CUDA_CHECK(cudaEventSynchronize(stop));

    float memTimeMs = 0;
    CUDA_CHECK(cudaEventElapsedTime(&memTimeMs, start, stop));
    float avgMemTimeMs = memTimeMs / timedIterations;

    // Bandwidth calculation: 8 floats read + 8 floats written per thread for c1-c8 = 64 bytes read + 64 bytes written = 128 bytes per element
    double totalBytesTransferred = static_cast<double>(numElements) * (8 * sizeof(float) * 2);
    double memBandwidthGBs = (totalBytesTransferred / (avgMemTimeMs * 1e-3)) / 1e9;

    std::cout << "  Average Time : " << avgMemTimeMs << " ms\n";
    std::cout << "  Est. Bandwidth: " << memBandwidthGBs << " GB/s\n";

    // --- 2. Benchmark Compute-Bound Kernel ---
    std::cout << "\n[2/2] Running Compute-Bound Kernel..." << std::endl;
    for (int w = 0; w < warmupIterations; ++w) {
        computeBoundKernel<<<numBlocks, blockSize>>>(view, numElements);
    }
    CUDA_CHECK(cudaDeviceSynchronize());

    CUDA_CHECK(cudaEventRecord(start));
    for (int it = 0; it < timedIterations; ++it) {
        computeBoundKernel<<<numBlocks, blockSize>>>(view, numElements);
    }
    CUDA_CHECK(cudaEventRecord(stop));
    CUDA_CHECK(cudaEventSynchronize(stop));

    float compTimeMs = 0;
    CUDA_CHECK(cudaEventElapsedTime(&compTimeMs, start, stop));
    float avgCompTimeMs = compTimeMs / timedIterations;

    std::cout << "  Average Time : " << avgCompTimeMs << " ms\n";

    // Cleanup
    CUDA_CHECK(cudaEventDestroy(start));
    CUDA_CHECK(cudaEventDestroy(stop));
    CUDA_CHECK(cudaFree(d_buffer));

    std::cout << "\nBenchmark finished successfully.\n";
    return 0;
}