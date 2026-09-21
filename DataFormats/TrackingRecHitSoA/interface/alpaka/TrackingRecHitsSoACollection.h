#ifndef DataFormats_TrackingRecHitSoA_interface_alpaka_TrackingRecHitsSoACollection_h
#define DataFormats_TrackingRecHitSoA_interface_alpaka_TrackingRecHitsSoACollection_h

// #define GPU_DEBUG

#include <cstdint>
#include <type_traits>

#include <alpaka/alpaka.hpp>

#include "DataFormats/Portable/interface/alpaka/PortableCollection.h"
#include "DataFormats/TrackingRecHitSoA/interface/TrackingRecHitsDevice.h"
#include "DataFormats/TrackingRecHitSoA/interface/TrackingRecHitsHost.h"
#include "DataFormats/TrackingRecHitSoA/interface/TrackingRecHitsSoA.h"
#include "Geometry/CommonTopologies/interface/SimplePixelTopology.h"
#include "HeterogeneousCore/AlpakaInterface/interface/CopyToHost.h"
#include "HeterogeneousCore/AlpakaInterface/interface/CopyToDevice.h"
#include "HeterogeneousCore/AlpakaInterface/interface/config.h"
#include "HeterogeneousCore/AlpakaInterface/interface/concepts.h"

namespace ALPAKA_ACCELERATOR_NAMESPACE::reco {

  using TrackingRecHitsSoACollection = std::conditional_t<std::is_same_v<Device, alpaka::DevCpu>,
                                                          ::reco::TrackingRecHitHost,
                                                          ::reco::TrackingRecHitDevice<Device>>;
}  // namespace ALPAKA_ACCELERATOR_NAMESPACE::reco

ASSERT_DEVICE_MATCHES_HOST_COLLECTION(reco::TrackingRecHitsSoACollection, reco::TrackingRecHitHost);

#endif  // DataFormats_TrackingRecHitSoA_interface_alpaka_TrackingRecHitsSoACollection_h
