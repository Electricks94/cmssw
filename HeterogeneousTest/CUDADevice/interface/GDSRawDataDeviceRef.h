#ifndef HeterogeneousTest_CUDADevice_GDSRawDataDeviceRef_h
#define HeterogeneousTest_CUDADevice_GDSRawDataDeviceRef_h

#include <cstdint>

#include "HeterogeneousTest/CUDADevice/interface/FRDScan.h"

// Device-resident stand-in for FEDRawDataCollection.
//
// Owns nothing: a device pointer to the GDS chunk, a device pointer to this
// event's slice of the FED index, and the CUDA device those pointers live on.
//
// device() matters because the CUDA "current device" is per HOST THREAD, and a
// consumer runs on whatever TBB thread the framework gives it. Consumers must
// cudaSetDevice(ref.device()) before touching these pointers.

namespace gdsraw {

  class RawDataDeviceRef {
  public:
    RawDataDeviceRef() = default;
    RawDataDeviceRef(const unsigned char* chunk, const frdscan::FedEntry* feds, uint32_t nFeds, int device)
        : chunk_(chunk), feds_(feds), nFeds_(nFeds), device_(device) {}

    const unsigned char* chunk() const { return chunk_; }    // DEVICE pointer to raw bytes
    const frdscan::FedEntry* feds() const { return feds_; }  // DEVICE pointer to this event's index
    uint32_t nFeds() const { return nFeds_; }
    int device() const { return device_; }

  private:
    const unsigned char* chunk_ = nullptr;
    const frdscan::FedEntry* feds_ = nullptr;
    uint32_t nFeds_ = 0;
    int device_ = 0;
  };

}  // namespace gdsraw

#endif
