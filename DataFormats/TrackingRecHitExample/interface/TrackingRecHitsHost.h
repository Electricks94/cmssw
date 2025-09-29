#ifndef TrackingRecHitExample_interface_TrackingRecHitsHost_h
#define TrackingRecHitExample_interface_TrackingRecHitsHost_h

#include "DataFormats/Common/interface/MultiCollection.h"
#include "DataFormats/Common/interface/RefProd.h"

#include "DataFormats/Portable/interface/MultiView.h"
#include "DataFormats/Portable/interface/PortableHostCollection.h"

#include "DataFormats/TrackingRecHitExample/interface/TrackingRecHitsSoA.h"

namespace reco {
  using TrackingRecHitHostBlocks = PortableHostCollection<reco::TrackingBlocks>;
}

/*
template <typename Collection>
class TrackingCollectionManager {
public:

  template <typename... Args>
  explicit TrackingCollectionManager(Args&&... refs) : collectionManager_{refs...} {}

  template<typename T>
  [[nodiscard]] auto view() const {
    return collectionManager_.template makeFlatView<T>();
  }

  uint32_t nHits() const { return static_cast<uint32_t>( this->template view<reco::TrackingRecHitSoA>().size() ); }
  // each TrackingRecHitsSoACollection contains an extra module which we don't count here.
  // See TrackingRecHitsDevice.h for more explanation
  uint32_t nModules() const { return static_cast<uint32_t>( this->template view<reco::HitModuleSoA>().size() - 2 ); }

  int32_t offsetBPIX2() const {
    // Due to the detector layout only the offset from the first SoA (pixelRecHit) is usefull
    auto const& rp = collectionManager_.refProds()[0];
    auto const& trackingRecHitCollection = *rp;
    return trackingRecHitCollection.offsetBPIX2(); 
  }

private: 
    MultiCollection<Collection> collectionManager_;
};

*/

#endif // TrackingRecHitExample_interface_TrackingRecHitsHost_h
