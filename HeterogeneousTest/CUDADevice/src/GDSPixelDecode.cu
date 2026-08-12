#include <cuda_runtime.h>

#include "HeterogeneousTest/CUDADevice/interface/GDSPixelDecode.h"
#include "HeterogeneousCore/CUDAUtilities/interface/cudaCheck.h"

using namespace cms::cuda;
#define cudaCheck(ARG) cms::cuda::cudaCheck(__FILE__, __LINE__, __func__, (ARG))

// Bit layout transcribed from:
//   DataFormats/SiPixelDigi/interface/SiPixelDigiConstants.h
//   RecoLocalTracker/SiPixelClusterizer/plugins/alpaka/SiPixelRawToClusterKernel.h
//
//   word31..26 LINK | 25..21 ROC | 20..16 DCOL | 15..8 PXID | 7..0 ADC

namespace {

  constexpr uint32_t kAdcBits = 8;
  constexpr uint32_t kPxidBits = 8;
  constexpr uint32_t kDcolBits = 5;
  constexpr uint32_t kRocBits = 5;
  constexpr uint32_t kLinkBits = 6;

  constexpr uint32_t kAdcShift = 0;
  constexpr uint32_t kPxidShift = kAdcShift + kAdcBits;    // 8
  constexpr uint32_t kDcolShift = kPxidShift + kPxidBits;  // 16
  constexpr uint32_t kRocShift = kDcolShift + kDcolBits;   // 21
  constexpr uint32_t kLinkShift = kRocShift + kRocBits;    // 26

  constexpr uint32_t kAdcMask = ~(~uint32_t(0) << kAdcBits);
  constexpr uint32_t kPxidMask = ~(~uint32_t(0) << kPxidBits);
  constexpr uint32_t kDcolMask = ~(~uint32_t(0) << kDcolBits);
  constexpr uint32_t kRocMask = ~(~uint32_t(0) << kRocBits);
  constexpr uint32_t kLinkMask = ~(~uint32_t(0) << kLinkBits);

  constexpr uint32_t kMaxLink = 48;
  constexpr uint32_t kMaxRoc = 8;
  constexpr uint32_t kNumRowsInRoc = 80;
  constexpr uint32_t kNumColsInRoc = 52;

  __global__ void decodeKernel(const uint32_t* words,
                               const uint8_t* fedIndex,
                               uint32_t nWords,
                               uint32_t minFedId,
                               gdsdecode::Digi* digis,
                               uint32_t* counters) {
    const uint32_t i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= nWords)
      return;

    const uint32_t ww = words[i];

    const uint32_t link = (ww >> kLinkShift) & kLinkMask;
    const uint32_t roc = (ww >> kRocShift) & kRocMask;
    const uint32_t dcol = (ww >> kDcolShift) & kDcolMask;
    const uint32_t pxid = (ww >> kPxidShift) & kPxidMask;
    const uint32_t adc = (ww >> kAdcShift) & kAdcMask;

    // ROC-local coordinates (LocalPixel::rocRow/rocCol in the production code)
    const uint32_t rocRow = kNumRowsInRoc - pxid / 2;
    const uint32_t rocCol = dcol * 2 + pxid % 2;

    gdsdecode::Digi& d = digis[i];
    d.link = link;
    d.roc = roc;
    d.rocRow = rocRow;
    d.rocCol = rocCol;
    d.adc = adc;
    d.fedId = uint32_t(fedIndex[i / 2]) + minFedId;

    // classify: counters[0]=valid 1=badLink 2=badRoc 3=badCoord 4=zeroAdc
    if (ww == 0) {
      return;  // padding word, not counted either way
    }
    if (link == 0 || link > kMaxLink) {
      atomicAdd(&counters[1], 1u);
      return;
    }
    if (roc == 0 || roc > kMaxRoc) {
      atomicAdd(&counters[2], 1u);
      return;
    }
    if (rocRow >= kNumRowsInRoc || rocCol >= kNumColsInRoc) {
      atomicAdd(&counters[3], 1u);
      return;
    }
    if (adc == 0) {
      atomicAdd(&counters[4], 1u);
      return;
    }
    atomicAdd(&counters[0], 1u);
  }

}  // namespace

namespace gdsdecode {

  DecodeResult decodeWords(const uint32_t* d_words,
                           const uint8_t* d_fedIndex,
                           uint32_t nWords,
                           uint32_t minFedId) {
    DecodeResult r;
    r.nWords = nWords;
    if (nWords == 0)
      return r;

    cudaCheck(cudaMalloc(&r.d_digis, size_t(nWords) * sizeof(Digi)));
    uint32_t* d_counters = nullptr;
    cudaCheck(cudaMalloc(&d_counters, 5 * sizeof(uint32_t)));
    cudaCheck(cudaMemset(d_counters, 0, 5 * sizeof(uint32_t)));

    const int block = 256;
    const int grid = (nWords + block - 1) / block;
    decodeKernel<<<grid, block>>>(d_words, d_fedIndex, nWords, minFedId, r.d_digis, d_counters);
    cudaCheck(cudaGetLastError());
    cudaCheck(cudaDeviceSynchronize());

    uint32_t h[5] = {0, 0, 0, 0, 0};
    cudaCheck(cudaMemcpy(h, d_counters, 5 * sizeof(uint32_t), cudaMemcpyDeviceToHost));
    cudaFree(d_counters);

    r.nValid = h[0];
    r.nBadLink = h[1];
    r.nBadRoc = h[2];
    r.nBadCoord = h[3];
    r.nZeroAdc = h[4];
    return r;
  }

}  // namespace gdsdecode
