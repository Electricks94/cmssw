#ifndef TrackingRecHitExample_plugins_alpaka_TestKernel_h
#define TrackingRecHitExample_plugins_alpaka_TestKernel_h

#include "HeterogeneousCore/AlpakaInterface/interface/config.h"
#include "DataFormats/TrackingRecHitExample/interface/alpaka/DeviceCollectionSoATest.h"


namespace ALPAKA_ACCELERATOR_NAMESPACE {

  struct TestKernel {
    static void run(Queue& queue, DeviceCollectionManagerTracking& manager, float* result);
  };

}  // namespace ALPAKA_ACCELERATOR_NAMESPACE

#endif  // TrackingRecHitExample_plugins_alpaka_TestKernel_h
