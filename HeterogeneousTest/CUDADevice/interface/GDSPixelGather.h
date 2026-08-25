#ifndef HeterogeneousTest_CUDADevice_GDSPixelGather_h
#define HeterogeneousTest_CUDADevice_GDSPixelGather_h

#include <cstdint>

#include "HeterogeneousTest/CUDADevice/interface/GDSRawDataDeviceRef.h"

// The GPU counterpart of what SiPixelRawToCluster::acquire() does on the host:
// pick the fragments in the pixel FED range, skip each fragment's FED header and
// trailer, and concatenate the payload words into ONE flat buffer, plus a byte
// array tagging each 64-bit digi with its FED.
//
// All scratch memory lives in a Workspace that the CALLER allocates once and
// reuses. Allocating per event costs ~7 cudaMallocAsync calls, and with many
// host threads those serialise on the driver: profiling showed 88% of all CUDA
// API time in alloc/free while the GPU sat idle.

namespace gdsgather {

  // Preallocated scratch, one per EDM stream. Not thread safe: give each stream
  // its own.
  struct Workspace {
    uint32_t* d_counts = nullptr;    // maxFeds
    uint32_t* d_offsets = nullptr;   // maxFeds
    uint32_t* d_totals = nullptr;    // 2
    uint32_t* d_words = nullptr;     // maxWords
    uint8_t* d_fedIndex = nullptr;   // maxWords / 2 + 1
    uint32_t maxFeds = 0;
    uint32_t maxWords = 0;
  };

  void allocateWorkspace(Workspace& ws, uint32_t maxFeds, uint32_t maxWords);
  void freeWorkspace(Workspace& ws);

  // d_words / d_fedIndex point INTO the workspace and are not owned by the result
  struct GatherResult {
    uint32_t* d_words = nullptr;
    uint8_t* d_fedIndex = nullptr;
    uint32_t nWords = 0;      // == wordCounter in SiPixelRawToCluster
    uint32_t nSelected = 0;   // == fedCounter
  };

  GatherResult gatherFeds(const gdsraw::RawDataDeviceRef& ref,
                          uint32_t minFedId,
                          uint32_t maxFedId,
                          Workspace& ws);

}  // namespace gdsgather

#endif
