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
                    SOA_COLUMN(int32_t, c14),
                    SOA_COLUMN(float, s1),
                    SOA_COLUMN(float, s2),
                    SOA_COLUMN(float, s3),
                    SOA_COLUMN(float, s4),
                    SOA_COLUMN(float, s5),
                    SOA_COLUMN(float, s6),
                    SOA_COLUMN(float, s7),
                    SOA_COLUMN(float, s8),
                    SOA_COLUMN(int16_t, s9),
                    SOA_COLUMN(int32_t, s10),
                    SOA_COLUMN(int16_t, s11),
                    SOA_COLUMN(int16_t, s12),
                    SOA_COLUMN(uint16_t, s13),
                    SOA_COLUMN(int32_t, s14),
                    SOA_COLUMN(float, a1),
                    SOA_COLUMN(float, a2),
                    SOA_COLUMN(float, a3),
                    SOA_COLUMN(float, a4),
                    SOA_COLUMN(float, a5),
                    SOA_COLUMN(float, a6),
                    SOA_COLUMN(float, a7),
                    SOA_COLUMN(float, a8),
                    SOA_COLUMN(int16_t, a9),
                    SOA_COLUMN(int32_t, a10),
                    SOA_COLUMN(int16_t, a11),
                    SOA_COLUMN(int16_t, a12),
                    SOA_COLUMN(uint16_t, a13),
                    SOA_COLUMN(int32_t, a14))

using SoA = SoATemplate<>;
using View = SoA::ViewTemplate<cms::soa::RestrictQualify::enabled, cms::soa::RangeChecking::extended>;
using ConstView = SoA::ConstViewTemplate<cms::soa::RestrictQualify::enabled, cms::soa::RangeChecking::extended>;

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

/*
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
        v1 = fmaf(in[i].c1(), decay, b2);
        v2 = fmaf(in[i].c2(), decay, b3);
        v3 = fmaf(in[i].c3(), decay, b4);
        v4 = fmaf(in[i].c4(), decay, b5);
        v5 = fmaf(in[i].c5(), decay, b6);
        v6 = fmaf(in[i].c6(), decay, b7);
        v7 = fmaf(in[i].c7(), decay, b8);
        v8 = fmaf(in[i].c9(), decay, b1);
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
*/

__global__ void computeBoundKernel(ConstView in, View out, int iters) {
  const int i = blockIdx.x * blockDim.x + threadIdx.x;
  const int total_size = in.metadata().size();

  if (i >= total_size) return;

  constexpr float alpha = 0.9999f;
  constexpr float beta  = 0.0001f;

  // Initial state loaded from direct local SoA slot
  float x0  = in[i].c1();
  float x1  = in[i].c2();
  float x2  = in[i].c3();
  float x3  = in[i].c4();
  float x4  = in[i].c5();
  float x5  = in[i].c6();
  float x6  = in[i].c7();
  float x7  = in[i].c8();

  // Auxiliary state variables to maximize register pressure
  float x8  = x0 * 0.5f + 0.1f;
  float x9  = x1 * 0.5f + 0.2f;
  float x10 = x2 * 0.5f + 0.3f;
  float x11 = x3 * 0.5f + 0.4f;
  float x12 = x4 * 0.5f + 0.5f;
  float x13 = x5 * 0.5f + 0.6f;
  float x14 = x6 * 0.5f + 0.7f;
  float x15 = x7 * 0.5f + 0.8f;

  // 1. Fetch direct local values from the SoA
  float in_c1 = in[i].c1();
  float in_c2 = in[i].c2();
  float in_c3 = in[i].c3();
  float in_c4 = in[i].c4();
  float in_c5 = in[i].c5();
  float in_c6 = in[i].c6();
  float in_c7 = in[i].c7();
  float in_c8 = in[i].c8();

  // 3. Indirect memory lookup into a different location of the SoA
  float ind_c1 = in[in[i].c10()].c1();
  float ind_c2 = in[in[i].c10()].c2();
  float ind_c3 = in[in[i].c10()].c3();
  float ind_c4 = in[in[i].c10()].c4();
  float ind_c5 = in[in[i].c10()].c5();
  float ind_c6 = in[in[i].c10()].c6();
  float ind_c7 = in[in[i].c10()].c7();
  float ind_c8 = in[in[i].c10()].c8();

  // 4. Non-linear update blending direct SoA, indirect dynamic SoA, and transcendental functions
  float nx0  = fmaf(alpha, x0,  sinf(x15) * beta - (in_c1 + ind_c1) * 0.005f);
  float nx1  = fmaf(alpha, x1,  cosf(x0)  * beta - (in_c2 + ind_c2) * 0.005f);
  float nx2  = fmaf(alpha, x2,  tanhf(x1) * beta - (in_c3 + ind_c3) * 0.005f);
  float nx3  = fmaf(alpha, x3,  sinf(x2)  * beta - (in_c4 + ind_c4) * 0.005f);
  float nx4  = fmaf(alpha, x4,  cosf(x3)  * beta - (in_c5 + ind_c5) * 0.005f);
  float nx5  = fmaf(alpha, x5,  sinf(x4)  * beta - (in_c6 + ind_c6) * 0.005f);
  float nx6  = fmaf(alpha, x6,  cosf(x5)  * beta - (in_c7 + ind_c7) * 0.005f);
  float nx7  = fmaf(alpha, x7,  sinf(x6)  * beta - (in_c8 + ind_c8) * 0.005f);
  
  float nx8  = fmaf(alpha, x8,  cosf(x7)  * beta - (in_c1 * ind_c8) * 0.005f);
  float nx9  = fmaf(alpha, x9,  sinf(x8)  * beta - (in_c2 * ind_c7) * 0.005f);
  float nx10 = fmaf(alpha, x10, cosf(x9)  * beta - (in_c3 * ind_c6) * 0.005f);
  float nx11 = fmaf(alpha, x11, sinf(x10) * beta - (in_c4 * ind_c5) * 0.005f);
  float nx12 = fmaf(alpha, x12, cosf(x11) * beta - (in_c5 * ind_c4) * 0.005f);
  float nx13 = fmaf(alpha, x13, sinf(x12) * beta - (in_c6 * ind_c3) * 0.005f);
  float nx14 = fmaf(alpha, x14, cosf(x13) * beta - (in_c7 * ind_c2) * 0.005f);
  float nx15 = fmaf(alpha, x15, sinf(x14) * beta - (in_c8 * ind_c1) * 0.005f);

  // 5. Cross-coupling state matrix update
  x0  = nx0  * 0.9f + nx1  * 0.1f;
  x1  = nx1  * 0.9f + nx2  * 0.1f;
  x2  = nx2  * 0.9f + nx3  * 0.1f;
  x3  = nx3  * 0.9f + nx4  * 0.1f;
  x4  = nx4  * 0.9f + nx5  * 0.1f;
  x5  = nx5  * 0.9f + nx6  * 0.1f;
  x6  = nx6  * 0.9f + nx7  * 0.1f;
  x7  = nx7  * 0.9f + nx8  * 0.1f;
  x8  = nx8  * 0.9f + nx9  * 0.1f;
  x9  = nx9  * 0.9f + nx10 * 0.1f;
  x10 = nx10 * 0.9f + nx11 * 0.1f;
  x11 = nx11 * 0.9f + nx12 * 0.1f;
  x12 = nx12 * 0.9f + nx13 * 0.1f;
  x13 = nx13 * 0.9f + nx14 * 0.1f;
  x14 = nx14 * 0.9f + nx15 * 0.1f;
  x15 = nx15 * 0.9f + nx0  * 0.1f;

  // Write back outputs
  out[i].c1() = x0;
  out[i].c2() = x1;
  out[i].c3() = x2;
  out[i].c4() = x3;
  out[i].c5() = x4;
  out[i].c6() = x5;
  out[i].c7() = x6;
  out[i].c8() = x7;
}

__global__ void computeBoundKernelTest(ConstView in, View out, int iters) {
  const int i = blockIdx.x * blockDim.x + threadIdx.x;
  const int total_size = in.metadata().size();

  if (i >= total_size) return;

  constexpr float alpha = 0.9999f;
  constexpr float beta  = 0.0001f;

  // Initial state loaded from direct local SoA slot
  const auto element_i = in[i];
  float x0  = element_i.c1();
  float x1  = element_i.c2();
  float x2  = element_i.c3();
  float x3  = element_i.c4();
  float x4  = element_i.c5();
  float x5  = element_i.c6();
  float x6  = element_i.c7();
  float x7  = element_i.c8();

  // Auxiliary state variables to maximize register pressure
  float x8  = x0 * 0.5f + 0.1f;
  float x9  = x1 * 0.5f + 0.2f;
  float x10 = x2 * 0.5f + 0.3f;
  float x11 = x3 * 0.5f + 0.4f;
  float x12 = x4 * 0.5f + 0.5f;
  float x13 = x5 * 0.5f + 0.6f;
  float x14 = x6 * 0.5f + 0.7f;
  float x15 = x7 * 0.5f + 0.8f;

  // 1. Fetch direct local values from the SoA
  float in_c1 = element_i.c1();
  float in_c2 = element_i.c2();
  float in_c3 = element_i.c3();
  float in_c4 = element_i.c4();
  float in_c5 = element_i.c5();
  float in_c6 = element_i.c6();
  float in_c7 = element_i.c7();
  float in_c8 = element_i.c8();

  // 3. Indirect memory lookup into a different location of the SoA
  const auto element_ii = in[element_i.c10()];
  float ind_c1 = element_ii.c1();
  float ind_c2 = element_ii.c2();
  float ind_c3 = element_ii.c3();
  float ind_c4 = element_ii.c4();
  float ind_c5 = element_ii.c5();
  float ind_c6 = element_ii.c6();
  float ind_c7 = element_ii.c7();
  float ind_c8 = element_ii.c8();

  // 4. Non-linear update blending direct SoA, indirect dynamic SoA, and transcendental functions
  float nx0  = fmaf(alpha, x0,  sinf(x15) * beta - (in_c1 + ind_c1) * 0.005f);
  float nx1  = fmaf(alpha, x1,  cosf(x0)  * beta - (in_c2 + ind_c2) * 0.005f);
  float nx2  = fmaf(alpha, x2,  tanhf(x1) * beta - (in_c3 + ind_c3) * 0.005f);
  float nx3  = fmaf(alpha, x3,  sinf(x2)  * beta - (in_c4 + ind_c4) * 0.005f);
  float nx4  = fmaf(alpha, x4,  cosf(x3)  * beta - (in_c5 + ind_c5) * 0.005f);
  float nx5  = fmaf(alpha, x5,  sinf(x4)  * beta - (in_c6 + ind_c6) * 0.005f);
  float nx6  = fmaf(alpha, x6,  cosf(x5)  * beta - (in_c7 + ind_c7) * 0.005f);
  float nx7  = fmaf(alpha, x7,  sinf(x6)  * beta - (in_c8 + ind_c8) * 0.005f);
  
  float nx8  = fmaf(alpha, x8,  cosf(x7)  * beta - (in_c1 * ind_c8) * 0.005f);
  float nx9  = fmaf(alpha, x9,  sinf(x8)  * beta - (in_c2 * ind_c7) * 0.005f);
  float nx10 = fmaf(alpha, x10, cosf(x9)  * beta - (in_c3 * ind_c6) * 0.005f);
  float nx11 = fmaf(alpha, x11, sinf(x10) * beta - (in_c4 * ind_c5) * 0.005f);
  float nx12 = fmaf(alpha, x12, cosf(x11) * beta - (in_c5 * ind_c4) * 0.005f);
  float nx13 = fmaf(alpha, x13, sinf(x12) * beta - (in_c6 * ind_c3) * 0.005f);
  float nx14 = fmaf(alpha, x14, cosf(x13) * beta - (in_c7 * ind_c2) * 0.005f);
  float nx15 = fmaf(alpha, x15, sinf(x14) * beta - (in_c8 * ind_c1) * 0.005f);

  // 5. Cross-coupling state matrix update
  x0  = nx0  * 0.9f + nx1  * 0.1f;
  x1  = nx1  * 0.9f + nx2  * 0.1f;
  x2  = nx2  * 0.9f + nx3  * 0.1f;
  x3  = nx3  * 0.9f + nx4  * 0.1f;
  x4  = nx4  * 0.9f + nx5  * 0.1f;
  x5  = nx5  * 0.9f + nx6  * 0.1f;
  x6  = nx6  * 0.9f + nx7  * 0.1f;
  x7  = nx7  * 0.9f + nx8  * 0.1f;
  x8  = nx8  * 0.9f + nx9  * 0.1f;
  x9  = nx9  * 0.9f + nx10 * 0.1f;
  x10 = nx10 * 0.9f + nx11 * 0.1f;
  x11 = nx11 * 0.9f + nx12 * 0.1f;
  x12 = nx12 * 0.9f + nx13 * 0.1f;
  x13 = nx13 * 0.9f + nx14 * 0.1f;
  x14 = nx14 * 0.9f + nx15 * 0.1f;
  x15 = nx15 * 0.9f + nx0  * 0.1f;

  // Write back outputs
  auto out_i = out[i];
  out_i.c1() = x0;
  out_i.c2() = x1;
  out_i.c3() = x2;
  out_i.c4() = x3;
  out_i.c5() = x4;
  out_i.c6() = x5;
  out_i.c7() = x6;
  out_i.c8() = x7;
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

  const auto element = h_view[0];

  std::cout << "Size of View: " << sizeof(h_view) << std::endl;
  std::cout << "Size of element: " << sizeof(element) << std::endl;

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
    constexpr int32_t STRIDE = 1024; // Change to your desired fixed stride
    h_view[i].c10() = static_cast<int32_t>((i + numElements / 2 + (i % STRIDE)) % numElements);
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

  if (mode == 3) {
    // --- 2. Compute-bound kernel: tunable FMA workload ---
    std::cout << "\n Benchmark Compute-bound Test kernel \n";
    const float ms = timeKernel(
          [&] { computeBoundKernelTest<<<numBlocks, blockSize>>>(inCView, outView, iterSweep); },
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
