#ifndef TrackingRecHitExample_interface_alpaka_TrackingRecHitsSoACollection_h
#define TrackingRecHitExample_interface_alpaka_TrackingRecHitsSoACollection_h

#include <alpaka/alpaka.hpp>

#include "DataFormats/Common/interface/MultiCollection.h"
#include "DataFormats/Portable/interface/alpaka/PortableCollection.h"

#include "DataFormats/TrackingRecHitExample/interface/TrackingRecHitsSoA.h"
#include "DataFormats/TrackingRecHitExample/interface/TrackingRecHitsHost.h"
#include "DataFormats/TrackingRecHitExample/interface/TrackingRecHitsDevice.h"

#include "HeterogeneousCore/AlpakaInterface/interface/CopyToHost.h"
#include "HeterogeneousCore/AlpakaInterface/interface/CopyToDevice.h"
#include "HeterogeneousCore/AlpakaInterface/interface/config.h"

namespace ALPAKA_ACCELERATOR_NAMESPACE::reco {

  using TrackingRecHitsSoACollectionBlocks = std::conditional_t<std::is_same_v<Device, alpaka::DevCpu>,
                                                          ::reco::TrackingRecHitHostBlocks,
                                                          ::reco::TrackingRecHitDeviceBlocks<Device>>;

  using TrackingRecHitsMultiCollection = MultiCollection<TrackingRecHitsSoACollectionBlocks>;
}  // namespace ALPAKA_ACCELERATOR_NAMESPACE::reco

namespace cms::alpakatools {
  template <typename TDevice>
  struct CopyToHost<::reco::TrackingRecHitDeviceBlocks<TDevice>> {
    template <typename TQueue>
    static auto copyAsync(TQueue &queue, ::reco::TrackingRecHitDeviceBlocks<TDevice> const &srcData) {
      reco::TrackingRecHitHostBlocks dstData(queue, srcData->metadata().size());
      alpaka::memcpy(queue, dstData.buffer(), srcData.buffer());
      return dstData;
    }
  };
}  // namespace cms::alpakatools

ASSERT_DEVICE_MATCHES_HOST_COLLECTION(reco::TrackingRecHitsSoACollectionBlocks, reco::TrackingRecHitHostBlocks);

#endif  // TrackingRecHitExample_interface_alpaka_TrackingRecHitsSoACollection_h