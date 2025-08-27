#ifndef NGTSoATest_plugins_alpaka_TestKernel_h
#define NGTSoATest_plugins_alpaka_TestKernel_h

#include "HeterogeneousCore/AlpakaInterface/interface/config.h"
#include "DataFormats/NGTSoATest/interface/alpaka/DeviceCollectionSoATest.h"


namespace ALPAKA_ACCELERATOR_NAMESPACE {

  struct TestKernel {
    static void run(Queue& queue, MultiCollectionManager<SoADeviceCollection>& manager, float* result);
  };

}  // namespace ALPAKA_ACCELERATOR_NAMESPACE

#endif  // NGTSoATest_plugins_alpaka_TestKernel_h
