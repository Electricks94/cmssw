#include <cuda_runtime.h>

#include "HeterogeneousTest/CUDADevice/interface/GDSPixelGather.h"
#include "HeterogeneousCore/CUDAUtilities/interface/cudaCheck.h"

using namespace cms::cuda;
#define cudaCheck(ARG) cms::cuda::cudaCheck(__FILE__, __LINE__, __func__, (ARG))

namespace {

  constexpr uint32_t kFedHeaderLen = 8;   // sizeof(fedh_t)
  constexpr uint32_t kFedTrailerLen = 8;  // sizeof(fedt_t)

  __device__ __forceinline__ uint32_t rd32(const unsigned char* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
  }

  // payload words of one fragment: everything between its FED header and trailer.
  // NOTE: the host version skips ALL headers / stops before ALL trailers (its
  // moreHeaders / moreTrailers loops). This assumes exactly one of each -- true
  // for normal pixel data, but verify before trusting it in production.
  __device__ __forceinline__ uint32_t payloadWords(const frdscan::FedEntry& f) {
    if (f.size <= kFedHeaderLen + kFedTrailerLen)
      return 0;
    return (f.size - kFedHeaderLen - kFedTrailerLen) / 4u;
  }

  // ---- pass 1: word count per fragment, 0 for fragments outside the FED range ----
  // host equivalent: the `words[i] = (ew - bw); wordCounter += ...` bookkeeping
  __global__ void countKernel(const frdscan::FedEntry* feds,
                              uint32_t nFeds,
                              uint32_t minFedId,
                              uint32_t maxFedId,
                              uint32_t* counts) {
    const uint32_t i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= nFeds)
      return;
    const frdscan::FedEntry& f = feds[i];
    counts[i] = (f.fedId >= minFedId && f.fedId <= maxFedId) ? payloadWords(f) : 0u;
  }

  // ---- exclusive prefix sum, on device (one thread; nFeds is ~hundreds) ----
  // host equivalent: `index[i] = wordCounter` accumulating through the loop
  __global__ void scanKernel(const uint32_t* counts, uint32_t nFeds, uint32_t* offsets, uint32_t* totals) {
    if (threadIdx.x != 0 || blockIdx.x != 0)
      return;
    uint32_t running = 0, nSelected = 0;
    for (uint32_t i = 0; i < nFeds; ++i) {
      offsets[i] = running;
      running += counts[i];
      if (counts[i] > 0)
        ++nSelected;
    }
    totals[0] = running;    // nWords  == wordCounter
    totals[1] = nSelected;  // == fedCounter
  }

  // ---- pass 2: copy payload words + write fedId tags, one block per fragment ----
  // host equivalent: WordFedAppender::initializeWordFed's memcpy + memset
  __global__ void fillKernel(const unsigned char* chunk,
                             const frdscan::FedEntry* feds,
                             uint32_t nFeds,
                             uint32_t minFedId,
                             uint32_t maxFedId,
                             const uint32_t* offsets,
                             uint32_t* words,
                             uint8_t* fedIndex) {
    const uint32_t i = blockIdx.x;
    if (i >= nFeds)
      return;
    const frdscan::FedEntry& f = feds[i];
    if (f.fedId < minFedId || f.fedId > maxFedId)
      return;

    const uint32_t n = payloadWords(f);
    const unsigned char* src = chunk + f.offset + kFedHeaderLen;
    const uint32_t base = offsets[i];
    const uint8_t tag = uint8_t(f.fedId - minFedId);

    for (uint32_t w = threadIdx.x; w < n; w += blockDim.x) {
      words[base + w] = rd32(src + size_t(w) * 4);
      if ((w & 1u) == 0u)             // one tag byte per 64-bit digi = per word pair
        fedIndex[(base + w) / 2] = tag;
    }
  }

}  // namespace

namespace gdsgather {

  GatherResult gatherFeds(const gdsraw::RawDataDeviceRef& ref, uint32_t minFedId, uint32_t maxFedId) {
    GatherResult r;
    const uint32_t nFeds = ref.nFeds();
    if (nFeds == 0)
      return r;

    uint32_t *d_counts = nullptr, *d_offsets = nullptr, *d_totals = nullptr;
    cudaCheck(cudaMalloc(&d_counts, size_t(nFeds) * sizeof(uint32_t)));
    cudaCheck(cudaMalloc(&d_offsets, size_t(nFeds) * sizeof(uint32_t)));
    cudaCheck(cudaMalloc(&d_totals, 2 * sizeof(uint32_t)));

    const int block = 128;
    const int grid = (nFeds + block - 1) / block;
    countKernel<<<grid, block>>>(ref.feds(), nFeds, minFedId, maxFedId, d_counts);
    cudaCheck(cudaGetLastError());

    scanKernel<<<1, 1>>>(d_counts, nFeds, d_offsets, d_totals);
    cudaCheck(cudaGetLastError());
    cudaCheck(cudaDeviceSynchronize());

    uint32_t totals[2] = {0, 0};
    cudaCheck(cudaMemcpy(totals, d_totals, 2 * sizeof(uint32_t), cudaMemcpyDeviceToHost));
    r.nWords = totals[0];
    r.nSelected = totals[1];

    if (r.nWords > 0) {
      cudaCheck(cudaMalloc(&r.d_words, size_t(r.nWords) * sizeof(uint32_t)));
      cudaCheck(cudaMalloc(&r.d_fedIndex, size_t(r.nWords / 2 + 1) * sizeof(uint8_t)));
      fillKernel<<<nFeds, 256>>>(
          ref.chunk(), ref.feds(), nFeds, minFedId, maxFedId, d_offsets, r.d_words, r.d_fedIndex);
      cudaCheck(cudaGetLastError());
      cudaCheck(cudaDeviceSynchronize());
    }

    cudaFree(d_counts);
    cudaFree(d_offsets);
    cudaFree(d_totals);
    return r;
  }

}  // namespace gdsgather

