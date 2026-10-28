#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <memory>
#include <vector>

#include "HeterogeneousCore/CUDAUtilities/interface/cudaCheck.h"
#include "HeterogeneousCore/CUDAUtilities/interface/requireDevices.h"

#include "DataFormats/SoATemplate/interface/SoALayout.h"

// ---------------------------------------------------------------------------
// SoA definition
// ---------------------------------------------------------------------------
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
using View = SoA::ViewTemplate<cms::soa::RestrictQualify::enabled, cms::soa::RangeChecking::disabled>;
using ConstView = SoA::ConstViewTemplate<cms::soa::RestrictQualify::enabled, cms::soa::RangeChecking::disabled>;

// Payload of one element, derived from the types above (not hard-coded)
constexpr size_t kBytesPerElement = 8 * sizeof(float) + 4 * sizeof(int16_t) + 2 * sizeof(int32_t);  // 48 B
constexpr size_t kMemKernelBytesPerElement = 2 * kBytesPerElement;  // read all + write all = 96 B

__global__ void memoryBoundKernel(ConstView in, View out) {
  if( (blockIdx.x * blockDim.x + threadIdx.x) < in.metadata().size())
  {
    const int i = blockIdx.x * blockDim.x + threadIdx.x;

    const float f1 = in[i].c1(), f2 = in[i].c2(), f3 = in[i].c3(), f4 = in[i].c4();
    const float f5 = in[i].c5(), f6 = in[i].c6(), f7 = in[i].c7(), f8 = in[i].c8();
    const int16_t s9 = in[i].c9();
    const int32_t s10 = in[i].c10();
    const int16_t s11 = in[i].c11();
    const int16_t s12 = in[i].c12();
    const uint16_t u13 = in[i].c13();
    const int32_t s14 = in[i].c14();

    out[i].c1() = fmaf(f2, 1.001f, f1);
    out[i].c2() = fmaf(f3, 1.001f, f2);
    out[i].c3() = fmaf(f4, 1.001f, f3);
    out[i].c4() = fmaf(f5, 1.001f, f4);
    out[i].c5() = fmaf(f6, 1.001f, f5);
    out[i].c6() = fmaf(f7, 1.001f, f6);
    out[i].c7() = fmaf(f8, 1.001f, f7);
    out[i].c8() = fmaf(f1, 1.001f, f8);

    out[i].c9() = static_cast<int16_t>(s9 + s11);
    out[i].c10() = s10 + s14;
    out[i].c11() = static_cast<int16_t>(s11 - s12);
    out[i].c12() = static_cast<int16_t>(s12 + static_cast<int16_t>(u13));
    out[i].c13() = static_cast<uint16_t>(u13 + static_cast<uint16_t>(s9));
    out[i].c14() = s14 ^ s10;
  }
}

__global__ void computeBoundKernel(ConstView in, View out, int iters) {
  if( (blockIdx.x * blockDim.x + threadIdx.x) < in.metadata().size())
  {
    const int i = blockIdx.x * blockDim.x + threadIdx.x;
    constexpr float decay = 0.9999f;

    const float b1 = in[i].c1();
    const float b2 = in[i].c2();
    const float b3 = in[i].c3();
    const float b4 = in[i].c4();
    const float b5 = in[i].c5();
    const float b6 = in[i].c6();
    const float b7 = in[i].c7();
    const float b8 = in[i].c8();

    float v1 = b1;
    float v2 = b2;
    float v3 = b3;
    float v4 = b4;
    float v5 = b5;
    float v6 = b6;
    float v7 = b7;
    float v8 = b8;

    for (int it = 0; it < iters; ++it) {
        v1 = fmaf(v1, decay, b2);
        v2 = fmaf(v2, decay, b3);
        v3 = fmaf(v3, decay, b4);
        v4 = fmaf(v4, decay, b5);
        v5 = fmaf(v5, decay, b6);
        v6 = fmaf(v6, decay, b7);
        v7 = fmaf(v7, decay, b8);
        v8 = fmaf(v8, decay, b1);
    }

    out[i].c1() = v1;
    out[i].c2() = v2;
    out[i].c3() = v3;
    out[i].c4() = v4;
    out[i].c5() = v5;
    out[i].c6() = v6;
    out[i].c7() = v7;
    out[i].c8() = v8;
  }
}

// ---------------------------------------------------------------------------
// Host helpers
// ---------------------------------------------------------------------------

// Average time per launch in ms, with warm-up
template <typename Launch>
float timeKernel(Launch&& launch, int warmup, int iterations) {
  cudaEvent_t start, stop;
  CUDA_CHECK(cudaEventCreate(&start));
  CUDA_CHECK(cudaEventCreate(&stop));

  for (int w = 0; w < warmup; ++w)
    launch();
  CUDA_CHECK(cudaGetLastError());
  CUDA_CHECK(cudaDeviceSynchronize());

  if (iterations == 0){
    return 0.0f;
  }

  CUDA_CHECK(cudaEventRecord(start));
  for (int it = 0; it < iterations; ++it)
    launch();
  CUDA_CHECK(cudaEventRecord(stop));
  CUDA_CHECK(cudaEventSynchronize(stop));
  CUDA_CHECK(cudaGetLastError());

  float ms = 0.f;
  CUDA_CHECK(cudaEventElapsedTime(&ms, start, stop));
  CUDA_CHECK(cudaEventDestroy(start));
  CUDA_CHECK(cudaEventDestroy(stop));
  return ms / iterations;
}

int main(int argc, char** argv) {
  const int mode = (argc > 1) ? std::atoi(argv[1]) : 1;

  const int numElements = (argc > 2) ? std::atoi(argv[2]) : 4 * 1024 * 1024;
  const int iterSweep = (argc > 3) ? std::atoi(argv[3]) : 4;
  const int warmupIterations = (argc > 4) ? std::atoi(argv[4]) : 10;
  const int timedIterations = (argc > 5) ? std::atoi(argv[5]) : 100;

  std::cout << "Number of elements: " << numElements << std::endl;
  std::cout << "Inner iterations compute bound kernel : " << iterSweep << std::endl;
  std::cout << "warmupIterations : " << warmupIterations << std::endl;
  std::cout << "timedIterations : " << timedIterations << std::endl;

  constexpr int blockSize = 256;
  const int numBlocks = (numElements + blockSize - 1) / blockSize;

  int deviceCount = 0;
  CUDA_CHECK(cudaGetDeviceCount(&deviceCount));
  if (deviceCount == 0) {
    std::cerr << "No CUDA devices found.\n";
    return EXIT_FAILURE;
  }
  cudaDeviceProp prop;
  CUDA_CHECK(cudaGetDeviceProperties(&prop, 0));

  std::cout << std::fixed << std::setprecision(3);
  std::cout << "========================================\n";
  std::cout << "          SoA CUDA Benchmark\n";
  std::cout << "========================================\n";
  std::cout << "Device      : " << prop.name << "\n";
  std::cout << "Elements    : " << numElements << "\n";
  std::cout << "Block Size  : " << blockSize << "\n";
  std::cout << "Grid Size   : " << numBlocks << "\n";
  std::cout << "Bytes/elem  : " << kBytesPerElement << " (per SoA, all 14 columns)\n";

  // Separate input and output SoAs: the input is never modified, so every launch
  // does identical work and results are reproducible.
  const size_t bufferSize = SoA::computeDataSize(numElements);

  std::byte* h_buffer = nullptr;
  CUDA_CHECK(cudaMallocHost(&h_buffer, bufferSize));
  SoA h_soa(h_buffer, numElements);
  View h_view(h_soa);

    // fill up
  for (cms::soa::size_type i = 0; i < h_view.metadata().size(); i++) {
    h_view[i].c1() = static_cast<float>(i) * 1.0f;
    h_view[i].c2() = static_cast<float>(i) * 2.0f;
    h_view[i].c3() = static_cast<float>(i) * 3.0f;
    h_view[i].c4() = static_cast<float>(i) * 4.0f;
    h_view[i].c5() = static_cast<float>(i) * 5.0f;
    h_view[i].c6() = static_cast<float>(i) * 6.0f;
    h_view[i].c7() = static_cast<float>(i) * 7.0f;
    h_view[i].c8() = static_cast<float>(i) * 8.0f;

    h_view[i].c9() = static_cast<int16_t>(i) * 9;
    h_view[i].c10() = static_cast<int32_t>(i) * 10;
    h_view[i].c11() = static_cast<int16_t>(i) * 11;
    h_view[i].c12() = static_cast<int16_t>(i) * 12;
    h_view[i].c13() = static_cast<uint16_t>(i) * 13;
    h_view[i].c14() = static_cast<int32_t>(i) * 14;
  }

  std::byte *d_in = nullptr; 
  std::byte *d_out = nullptr;
  CUDA_CHECK(cudaMalloc(&d_in, bufferSize));
  CUDA_CHECK(cudaMalloc(&d_out, bufferSize));
  CUDA_CHECK(cudaMemset(d_out, 0, bufferSize));

  SoA inSoA(d_in, numElements);
  SoA outSoA(d_out, numElements);
  View inView(inSoA);
  ConstView inCView(inSoA);
  View outView(outSoA);

  CUDA_CHECK(cudaMemcpy(d_in, h_buffer, bufferSize, cudaMemcpyHostToDevice));

  CUDA_CHECK(cudaGetLastError());
  CUDA_CHECK(cudaDeviceSynchronize());

  if (mode == 1) {
    // --- 1. Memory-bound kernel: all 14 columns read, all 14 columns written ---
    std::cout << "\n Benchmark Memory-bound kernel \n";
    const float ms = timeKernel([&] { memoryBoundKernel<<<numBlocks, blockSize>>>(inCView, outView); },
                                warmupIterations, timedIterations);
    const double bytes = static_cast<double>(numElements) * kMemKernelBytesPerElement;
    std::cout << "  Average Time   : " << ms << " ms\n";
    std::cout << "  Bytes moved    : " << bytes / 1e6 << " MB per launch (" << kMemKernelBytesPerElement
              << " B/element)\n";
    std::cout << "  Eff. Bandwidth : " << bytes / (ms * 1e-3) / 1e9 << " GB/s\n";
  }

  if (mode == 2) {
    // --- 2. Compute-bound kernel: tunable FMA workload ---
    std::cout << "\n Benchmark Compute-bound kernel \n";
    const float ms = timeKernel(
          [&] { computeBoundKernel<<<numBlocks, blockSize>>>(inCView, outView, iterSweep); },
          warmupIterations,
          timedIterations);
      std::cout << "  Average Time   : " << ms << " ms\n";
  }


  CUDA_CHECK(cudaGetLastError());
  CUDA_CHECK(cudaDeviceSynchronize());

  CUDA_CHECK(cudaFreeHost(h_buffer));
  CUDA_CHECK(cudaFree(d_in));
  CUDA_CHECK(cudaFree(d_out));

  std::cout << "\nBenchmark finished successfully.\n";
  return 0;
}
