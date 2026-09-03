#include <cstdio>
#include <cstdlib>

#include <cuda_runtime.h>

#include "HeterogeneousTest/CUDADevice/interface/GDSPixelGather.h"
#include "HeterogeneousCore/CUDAUtilities/interface/cudaCheck.h"

using namespace cms::cuda;
#define cudaCheck(ARG) cms::cuda::cudaCheck(__FILE__, __LINE__, __func__, (ARG))

// HOST COUNTERPART: SiPixelRawToCluster.cc acquire(), the fedIds_ loop plus
// WordFedAppender::initializeWordFed. Same job, on the GPU, reading the GDS
// chunk in place.
//
// Everything runs on cudaStreamPerThread so concurrent EDM streams do not share
// the legacy default stream and never issue device-wide syncs.
//

namespace {

  constexpr uint32_t kFedHeaderLen = 8;   // sizeof(fedh_t)
  constexpr uint32_t kFedTrailerLen = 8;  // sizeof(fedt_t)

  __device__ __forceinline__ uint32_t rd32(const unsigned char* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
  }

  // payload words of one fragment: everything between its FED header and trailer.
  // NOTE: the host version skips ALL headers / stops before ALL trailers (its
  // moreHeaders / moreTrailers loops). This assumes exactly one of each.
  __device__ __forceinline__ uint32_t payloadWords(const frdscan::FedEntry& f) {
    if (f.size <= kFedHeaderLen + kFedTrailerLen)
      return 0;
    return (f.size - kFedHeaderLen - kFedTrailerLen) / 4u;
  }

  // pass1: word count per fragment, 0 outside the FED range 
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

  // exclusive prefix sum, on device (one thread; nFeds is ~hundreds) 
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
    totals[0] = running;    // nWords == wordCounter
    totals[1] = nSelected;  // == fedCounter
  }

  // pass 2: copy payload words + write fedId tags, one block per fragment 
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
      if ((w & 1u) == 0u)  // one tag byte per 64-bit digi = per word pair
        fedIndex[(base + w) / 2] = tag;
    }
  }

}  // namespace

namespace gdsgather {

  void allocateWorkspace(Workspace& ws, uint32_t maxFeds, uint32_t maxWords) {
    ws.maxFeds = maxFeds;
    ws.maxWords = maxWords;
    cudaCheck(cudaMalloc(&ws.d_counts, size_t(maxFeds) * sizeof(uint32_t)));
    cudaCheck(cudaMalloc(&ws.d_offsets, size_t(maxFeds) * sizeof(uint32_t)));
    cudaCheck(cudaMalloc(&ws.d_totals, 2 * sizeof(uint32_t)));
    cudaCheck(cudaMalloc(&ws.d_words, size_t(maxWords) * sizeof(uint32_t)));
    cudaCheck(cudaMalloc(&ws.d_fedIndex, (size_t(maxWords) / 2 + 1) * sizeof(uint8_t)));
  }

  void freeWorkspace(Workspace& ws) {
    if (ws.d_counts)
      cudaFree(ws.d_counts);
    if (ws.d_offsets)
      cudaFree(ws.d_offsets);
    if (ws.d_totals)
      cudaFree(ws.d_totals);
    if (ws.d_words)
      cudaFree(ws.d_words);
    if (ws.d_fedIndex)
      cudaFree(ws.d_fedIndex);
    ws = Workspace{};
  }

  GatherResult gatherFeds(const gdsraw::RawDataDeviceRef& ref,
                          uint32_t minFedId,
                          uint32_t maxFedId,
                          Workspace& ws) {
    GatherResult r;
    const uint32_t nFeds = ref.nFeds();
    if (nFeds == 0)
      return r;
    if (nFeds > ws.maxFeds) {
      printf("gatherFeds: nFeds %u exceeds workspace maxFeds %u -- raise maxFeds\n", nFeds, ws.maxFeds);
      abort();
    }

    cudaStream_t stream = cudaStreamPerThread;

    const int block = 128;
    const int grid = (nFeds + block - 1) / block;
    countKernel<<<grid, block, 0, stream>>>(ref.feds(), nFeds, minFedId, maxFedId, ws.d_counts);
    cudaCheck(cudaGetLastError());

    scanKernel<<<1, 1, 0, stream>>>(ws.d_counts, nFeds, ws.d_offsets, ws.d_totals);
    cudaCheck(cudaGetLastError());

    uint32_t totals[2] = {0, 0};
    cudaCheck(cudaMemcpyAsync(totals, ws.d_totals, 2 * sizeof(uint32_t), cudaMemcpyDeviceToHost, stream));
    cudaCheck(cudaStreamSynchronize(stream));
    r.nWords = totals[0];
    r.nSelected = totals[1];

    if (r.nWords > ws.maxWords) {
      printf("gatherFeds: nWords %u exceeds workspace maxWords %u -- raise maxWords\n", r.nWords, ws.maxWords);
      abort();
    }

    if (r.nWords > 0) {
      r.d_words = ws.d_words;        // borrowed, not owned
      r.d_fedIndex = ws.d_fedIndex;  // borrowed, not owned
      fillKernel<<<nFeds, 256, 0, stream>>>(
          ref.chunk(), ref.feds(), nFeds, minFedId, maxFedId, ws.d_offsets, r.d_words, r.d_fedIndex);
      cudaCheck(cudaGetLastError());
      cudaCheck(cudaStreamSynchronize(stream));
    }
    return r;
  }

}  // namespace gdsgather
