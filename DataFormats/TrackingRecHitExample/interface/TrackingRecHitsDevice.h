#ifndef TrackingRecHitExample_interface_TrackingRecHitsDevice_h
#define TrackingRecHitExample_interface_TrackingRecHitsDevice_h

#include <cstdint>

#include <alpaka/alpaka.hpp>

#include "DataFormats/Portable/interface/PortableDeviceCollection.h"

#include "DataFormats/TrackingRecHitExample/interface/TrackingRecHitsSoA.h"
#include "DataFormats/TrackingRecHitExample/interface/TrackingRecHitsHost.h"

namespace reco {

  template <typename TDev>
  using TrackingRecHitDeviceBlocks = PortableDeviceCollection<reco::TrackingBlocks, TDev>;

}

#endif // TrackingRecHitExample_interface_TrackingRecHitsDevice_h