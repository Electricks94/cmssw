#ifndef DataFormats_TrackingRecHitSoA_interface_TrackingRecHitsHost_h
#define DataFormats_TrackingRecHitSoA_interface_TrackingRecHitsHost_h

#include <cstdint>

#include <alpaka/alpaka.hpp>

#include "DataFormats/Common/interface/Uninitialized.h"
#include "DataFormats/Portable/interface/PortableHostCollection.h"
#include "DataFormats/SiPixelClusterSoA/interface/SiPixelClustersHost.h"
#include "DataFormats/TrackingRecHitSoA/interface/TrackingRecHitsSoA.h"
#include "DataFormats/TrivialSerialisation/interface/MemoryCopyTraits.h"
#include "HeterogeneousCore/AlpakaInterface/interface/config.h"


namespace reco {

  using TrackingRecHitHost = PortableHostCollection<reco::TrackingBlocksSoA>;

}  // namespace reco

#endif  // DataFormats_TrackingRecHitSoA_interface_TrackingRecHitsHost_h
