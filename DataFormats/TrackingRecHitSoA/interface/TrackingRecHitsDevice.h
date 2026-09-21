#ifndef DataFormats_TrackingRecHitSoA_interface_TrackingRecHitSoADevice_h
#define DataFormats_TrackingRecHitSoA_interface_TrackingRecHitSoADevice_h

#include <cstdint>

#include <alpaka/alpaka.hpp>

#include "DataFormats/Common/interface/Uninitialized.h"
#include "DataFormats/Portable/interface/PortableDeviceCollection.h"
#include "DataFormats/TrackingRecHitSoA/interface/TrackingRecHitsHost.h"
#include "DataFormats/TrackingRecHitSoA/interface/TrackingRecHitsSoA.h"
#include "DataFormats/SiPixelClusterSoA/interface/SiPixelClustersDevice.h"

namespace reco {

  template <typename TDev>
  using TrackingRecHitDevice = PortableDeviceCollection<TDev, reco::TrackingBlocksSoA>;

  template <typename TQueue, typename TCollection>
  requires requires(TCollection const& c) { c.view().trackingHits().offsetBPIX2(); }
  int32_t offsetBPIX2(TQueue queue, TCollection const& collection) {

    int32_t offset = 0;
    auto off_h = cms::alpakatools::make_host_view(offset);
    auto off_d = cms::alpakatools::make_device_view(queue, collection.view().trackingHits().offsetBPIX2());
    alpaka::memcpy(queue, off_h, off_d);

    return offset;
  }

}  // namespace reco

#endif  // DataFormats_TrackingRecHitSoA_interface_TrackingRecHitSoADevice_h
