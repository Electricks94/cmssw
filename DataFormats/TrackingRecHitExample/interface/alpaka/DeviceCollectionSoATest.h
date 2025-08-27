#ifndef TrackingRecHitExample_interface_DeviceCollectionTest_h
#define TrackingRecHitExample_interface_DeviceCollectionTest_h

#include "DataFormats/TrackingRecHitSoA/interface/alpaka/TrackingRecHitsSoACollection.h"
#include "DataFormats/TrackingRecHitExample/interface/HostCollectionSoATest.h"

using namespace reco;
using namespace ALPAKA_ACCELERATOR_NAMESPACE::reco;

namespace ALPAKA_ACCELERATOR_NAMESPACE {
    using DeviceCollectionManagerTracking = TrackingCollectionManager<TrackingRecHitsSoACollection>;
}


#endif  // TrackingRecHitExample_interface_DeviceCollectionTest_h
