#ifndef HeterogeneousTest_CUDADevice_GDSPixelGather_h
#define HeterogeneousTest_CUDADevice_GDSPixelGather_h

#include <cstdint>

#include "HeterogeneousTest/CUDADevice/interface/GDSRawDataDeviceRef.h"

namespace gdsgather {

  struct GatherResult {
    uint32_t* d_words = nullptr;    // device: concatenated 32-bit payload words
    uint8_t* d_fedIndex = nullptr;  // device: one byte per 64-bit digi = fedId - minFedId
    uint32_t nWords = 0;            // == wordCounter in SiPixelRawToCluster
    uint32_t nSelected = 0;         // == fedCounter
  };

  // Caller frees d_words / d_fedIndex with cudaFree.
  GatherResult gatherFeds(const gdsraw::RawDataDeviceRef& ref, uint32_t minFedId, uint32_t maxFedId);

}  // namespace gdsgather

#endif

