#ifndef HeterogeneousTest_CUDADevice_GDSRawDataDeviceRef_h
#define HeterogeneousTest_CUDADevice_GDSRawDataDeviceRef_h

#include <cstdint>

#include "HeterogeneousTest/CUDADevice/interface/FRDScan.h"

namespace gdsraw {

  class RawDataDeviceRef {
  public:
    RawDataDeviceRef() = default;
    RawDataDeviceRef(const unsigned char* chunk, const frdscan::FedEntry* feds, uint32_t nFeds)
        : chunk_(chunk), feds_(feds), nFeds_(nFeds) {}

    const unsigned char* chunk() const { return chunk_; }     // DEVICE pointer to the raw bytes
    const frdscan::FedEntry* feds() const { return feds_; }   // DEVICE pointer to this event's index
    uint32_t nFeds() const { return nFeds_; }

  private:
    const unsigned char* chunk_ = nullptr;
    const frdscan::FedEntry* feds_ = nullptr;
    uint32_t nFeds_ = 0;
  };

}  // namespace gdsraw

#endif

