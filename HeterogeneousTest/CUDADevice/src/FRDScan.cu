#include <cstdio>

#include <cuda_runtime.h>

#include "HeterogeneousTest/CUDADevice/interface/FRDScan.h"
#include "HeterogeneousCore/CUDAUtilities/interface/cudaCheck.h"

using namespace cms::cuda;
#define cudaCheck(ARG) cms::cuda::cudaCheck(__FILE__, __LINE__, __func__, (ARG))

namespace {

  constexpr uint32_t kFrdV6HeaderSize = 24;  // FRDHeaderVersionSize[6] = 6 * sizeof(uint32)
  constexpr int kOffFlags = 2;               // uint16
  constexpr int kOffRun = 4;                 // uint32
  constexpr int kOffLumi = 8;                // uint32
  constexpr int kOffEvent = 12;              // uint32
  constexpr int kOffEventSize = 16;          // uint32

  constexpr int kFedHeaderLen = 8;
  constexpr int kFedTrailerLen = 8;

  // fedh_t { uint32_t sourceid;  uint32_t eventid;  }  -> sourceid  at +0, eventid   at +4
  // fedt_t { uint32_t conscheck; uint32_t eventsize; }  -> conscheck at +0, eventsize at +4
  constexpr int kOffFedSourceId = 0;   // FEDHeader::sourceID() reads sourceid
  constexpr int kOffFedEventId = 4;    // FEDHeader::check() reads eventid
  constexpr int kOffFedEventSize = 4;  // FEDTrailer::fragmentLength() reads eventsize (SECOND word)

  constexpr uint32_t kSoidShift = 8;
  constexpr uint32_t kSoidWidth = 0x00000fff;  // FED_SOID: bits 8..19 of sourceid
  constexpr uint32_t kEvszShift = 0;
  constexpr uint32_t kEvszWidth = 0x00ffffff;  // FED_EVSZ: bits 0..23 of eventsize
  constexpr uint32_t kCtrlIdShift = 28;
  constexpr uint32_t kCtrlIdWidth = 0x0000000f;
  constexpr uint32_t kSlinkStartMarker = 0x5;  // FED_SLINK_START_MARKER, top nibble of eventid
  constexpr uint32_t kSlinkEndMarker = 0xa;    // FED_SLINK_END_MARKER,   top nibble of eventsize

  // unaligned little-endian reads: chunk offsets are not word aligned, so assemble
  // from bytes rather than dereferencing a uint32* (UB + can fault on the device).
  __device__ __forceinline__ uint16_t rd16(const unsigned char* p) {
    return uint16_t(p[0]) | (uint16_t(p[1]) << 8);
  }
  __device__ __forceinline__ uint32_t rd32(const unsigned char* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
  }

  // PHASE 1: sequential FRD event boundary walk (one thread) 
  // Serial by nature: each header's eventSize gives the offset of the next header.
  __global__ void scanEventsKernel(const unsigned char* chunk,
                                   uint64_t chunkSize,
                                   uint64_t firstHeaderOffset,
                                   uint32_t maxEvents,
                                   frdscan::EventRecord* events,
                                   uint32_t* nEventsOut) {
    if (threadIdx.x != 0 || blockIdx.x != 0)
      return;

    uint64_t pos = firstHeaderOffset;
    uint32_t n = 0;
    while (pos + kFrdV6HeaderSize <= chunkSize && n < maxEvents) {
      const unsigned char* h = chunk + pos;
      const uint32_t eventSize = rd32(h + kOffEventSize);
      const uint64_t payloadOffset = pos + kFrdV6HeaderSize;
      if (payloadOffset + eventSize > chunkSize)
        break;  // truncated tail

      frdscan::EventRecord& e = events[n];
      e.headerOffset = pos;
      e.payloadOffset = payloadOffset;
      e.payloadSize = eventSize;
      e.run = rd32(h + kOffRun);
      e.lumi = rd32(h + kOffLumi);
      e.event = rd32(h + kOffEvent);
      e.flags = rd16(h + kOffFlags);
      e.nFeds = 0;
      e.fedIndexBase = 0;
      e.truncated = 0;

      pos = payloadOffset + eventSize;
      ++n;
    }
    *nEventsOut = n;
  }

  // Walk one event's FED fragments backwards. feds == nullptr: count only.
  // Mirrors the host fillFEDRawDataCollection walk exactly.
  __device__ __forceinline__ void walkFeds(const unsigned char* chunk,
                                           frdscan::EventRecord& e,
                                           frdscan::FedEntry* feds /* may be nullptr */) {
    const unsigned char* payload = chunk + e.payloadOffset;
    int64_t remaining = e.payloadSize;
    uint32_t k = 0;
    uint32_t truncated = 0;
    while (remaining > 0) {
      const unsigned char* trailer = payload + remaining - kFedTrailerLen;
      const uint32_t evszWord = rd32(trailer + kOffFedEventSize);  // fedt_t.eventsize is the SECOND word
      if (((evszWord >> kCtrlIdShift) & kCtrlIdWidth) != kSlinkEndMarker) {
        truncated = 1;
        break;
      }
      const uint32_t fragWords = (evszWord >> kEvszShift) & kEvszWidth;
      const uint32_t fragSize = fragWords * 8u;  // FED_EVSZ counts 64-bit words
      if (fragSize < uint32_t(kFedHeaderLen + kFedTrailerLen) || int64_t(fragSize) > remaining) {
        truncated = 1;
        break;
      }
      const uint64_t fragStart = remaining - fragSize;
      if (feds) {
        const unsigned char* header = payload + fragStart;
        const uint32_t soidWord = rd32(header + kOffFedSourceId);  // fedh_t.sourceid is the FIRST word
        frdscan::FedEntry& f = feds[e.fedIndexBase + k];
        f.offset = uint32_t(e.payloadOffset + fragStart);
        f.size = fragSize;
        f.fedId = (soidWord >> kSoidShift) & kSoidWidth;
      }
      remaining -= fragSize;
      ++k;
    }
    e.nFeds = k;
    e.truncated = truncated;
  }

  // PHASE 2: count FEDs per event (one thread per event) 
  __global__ void countFedsKernel(const unsigned char* chunk, frdscan::EventRecord* events, uint32_t nEvents) {
    const uint32_t i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= nEvents)
      return;
    walkFeds(chunk, events[i], nullptr);
  }

  __global__ void prefixSumKernel(frdscan::EventRecord* events, uint32_t nEvents, uint32_t* totalOut) {
    if (threadIdx.x != 0 || blockIdx.x != 0)
      return;
    uint32_t running = 0;
    for (uint32_t i = 0; i < nEvents; ++i) {
      events[i].fedIndexBase = running;
      running += events[i].nFeds;
    }
    *totalOut = running;
  }

  //  PHASE 3: fill compact FED array (one thread per event) 
  // fedIndexBase has been set on the host via an exclusive prefix sum of nFeds.
  __global__ void fillFedsKernel(const unsigned char* chunk,
                                 frdscan::EventRecord* events,
                                 frdscan::FedEntry* feds,
                                 uint32_t nEvents) {
    const uint32_t i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= nEvents)
      return;
    walkFeds(chunk, events[i], feds);
  }

}  // namespace

namespace frdscan {

  ScanResult scanChunkOnDevice(const unsigned char* d_chunk,
                               uint64_t chunkSize,
                               uint64_t firstHeaderOffset,
                               uint32_t maxEvents) {
    EventRecord* d_events = nullptr;
    uint32_t* d_nEvents = nullptr;
    cudaCheck(cudaMalloc(&d_events, size_t(maxEvents) * sizeof(EventRecord)));
    cudaCheck(cudaMalloc(&d_nEvents, sizeof(uint32_t)));

    scanEventsKernel<<<1, 1>>>(d_chunk, chunkSize, firstHeaderOffset, maxEvents, d_events, d_nEvents);
    cudaCheck(cudaGetLastError());
    cudaCheck(cudaDeviceSynchronize());

    uint32_t nEvents = 0;
    cudaCheck(cudaMemcpy(&nEvents, d_nEvents, sizeof(uint32_t), cudaMemcpyDeviceToHost));
    cudaFree(d_nEvents);

    ScanResult result{d_events, nullptr, nEvents, 0};
    if (nEvents == 0)
      return result;

    const int block = 128;
    const int grid = (nEvents + block - 1) / block;

    countFedsKernel<<<grid, block>>>(d_chunk, d_events, nEvents);
    cudaCheck(cudaGetLastError());
    cudaCheck(cudaDeviceSynchronize());

    // exclusive prefix sum stays on the device; only the total (4 bytes) comes back
    uint32_t* d_total = nullptr;
    cudaCheck(cudaMalloc(&d_total, sizeof(uint32_t)));
    prefixSumKernel<<<1, 1>>>(d_events, nEvents, d_total);
    cudaCheck(cudaGetLastError());
    cudaCheck(cudaDeviceSynchronize());

    uint32_t totalFeds = 0;
    cudaCheck(cudaMemcpy(&totalFeds, d_total, sizeof(uint32_t), cudaMemcpyDeviceToHost));
    cudaFree(d_total);

    FedEntry* d_feds = nullptr;
    if (totalFeds > 0) {
      cudaCheck(cudaMalloc(&d_feds, size_t(totalFeds) * sizeof(FedEntry)));
      fillFedsKernel<<<grid, block>>>(d_chunk, d_events, d_feds, nEvents);
      cudaCheck(cudaGetLastError());
      cudaCheck(cudaDeviceSynchronize());
    }

    result.d_feds = d_feds;
    result.totalFeds = totalFeds;
    return result;
  }

}  // namespace frdscan

