#ifndef TrackingRecHitExample_interface_HostCollectionTest_h
#define TrackingRecHitExample_interface_HostCollectionTest_h

#include "CommonTools/RecoAlgos/interface/MultiCollectionManager.h"

#include "DataFormats/TrackingRecHitSoA/interface/TrackingRecHitsSoA.h"
#include "DataFormats/TrackingRecHitSoA/interface/TrackingRecHitsHost.h"
#include "DataFormats/TrackingRecHitSoA/interface/TrackingRecHitsDevice.h"
#include "DataFormats/Portable/interface/MultiSoAViewManager.h"

#include "DataFormats/Common/interface/RefProd.h"
#include "HeterogeneousCore/AlpakaInterface/interface/CopyToHost.h"

template <typename Collection, std::size_t N>
class TrackingCollectionManager {
public:

  template <typename... Args>
  explicit TrackingCollectionManager(Args&&... refs) : collectionManager_{refs...} {
    static_assert(sizeof...(Args) == 0 || sizeof...(Args) == N, "Number of arguments must be equal to N");  
  }

  template<typename T>
  [[nodiscard]] auto view() const {
    return collectionManager_.template makeFlatView<T>();
  }

  uint32_t nHits() const { return this->template view<reco::TrackingRecHitSoA>().size(); }
  // TODO: Do we need to encounter for a hidden last element here?
  uint32_t nModules() const { return this->template view<reco::HitModuleSoA>().size(); }

  int32_t offsetBPIX2() const {
    // Due to the detector layout only the offset from the first SoA (pixelRecHit) is usefull
    auto const& rp = collectionManager_.refProds()[0];
    auto const& trackingRecHitCollection = *rp;
    return trackingRecHitCollection.offsetBPIX2(); 
  }

private: 
    MultiCollectionManager<Collection, N> collectionManager_;
};

namespace cms::alpakatools {

  // Dummy function to be able to add the MultiCollectionManager to a device::EDPutToken
  // This function should never be called.
  template <typename Collection, std::size_t N>
  struct CopyToHost< TrackingCollectionManager<Collection, N> > {
    template <typename TQueue>
    static auto copyAsync(TQueue& /*queue*/, TrackingCollectionManager<Collection, N> const& deviceData) {
      assert(false && "The CopyToHost of the MultiCollectionManager must not be called!");
      return deviceData;
    }
  };

}  // namespace cms::alpakatools

#endif // TrackingRecHitExample_interface_HostCollectionTest_h
