#include "FWCore/ParameterSet/interface/ConfigurationDescriptions.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/ParameterSet/interface/ParameterSetDescription.h"
#include "FWCore/Utilities/interface/InputTag.h"
#include "FWCore/Utilities/interface/StreamID.h"

#include "HeterogeneousCore/AlpakaCore/interface/alpaka/global/EDProducer.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/Event.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/EventSetup.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/EDPutToken.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/EDGetToken.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/MakerMacros.h"

#include "DataFormats/TrackingRecHitExample/interface/TrackingRecHitsSoA.h"
#include "DataFormats/TrackingRecHitExample/interface/TrackingRecHitsHost.h"
#include "DataFormats/TrackingRecHitExample/interface/TrackingRecHitsDevice.h"
#include "DataFormats/TrackingRecHitExample/interface/alpaka/TrackingRecHitsSoACollection.h"

#include <iostream>

using namespace reco;
using namespace ALPAKA_ACCELERATOR_NAMESPACE::reco;

namespace ALPAKA_ACCELERATOR_NAMESPACE {

class SoAProducerTracking : public global::EDProducer<> {
public:
  SoAProducerTracking(edm::ParameterSet const& config)
      : EDProducer<>(config), soa1_{produces("SoAProduct1")}, soa2_{produces("SoAProduct2")} {}

  ~SoAProducerTracking(){};

  static void fillDescriptions(edm::ConfigurationDescriptions& descriptions) {}

  void produce(edm::StreamID sid, device::Event& event, device::EventSetup const&) const override {
    auto queue = event.queue();
    
    const cms::soa::size_type elementsTrackingHitsLayout = 1;
    const cms::soa::size_type elementsHitModulesLayout = 2;

    TrackingRecHitHostBlocks soaHost1(cms::alpakatools::host(), elementsTrackingHitsLayout, elementsHitModulesLayout);
    TrackingRecHitHostBlocks soaHost2(cms::alpakatools::host(), elementsTrackingHitsLayout, elementsHitModulesLayout);

    auto& view1 = soaHost1.view();
    auto& view2 = soaHost2.view();

    view1.TrackingHits().offsetBPIX2() = 100;
    view2.TrackingHits().offsetBPIX2() = 200;

    for (TrackingRecHitView::size_type i = 0; i < view1.TrackingHits().metadata().size(); ++i) {
      view1.TrackingHits()[i].xLocal() = 1.0f;
    }

    for (TrackingRecHitView::size_type i = 0; i < view2.TrackingHits().metadata().size(); ++i) {
      view2.TrackingHits()[i].xLocal() = 2.0f;
    }

    TrackingRecHitsSoACollectionBlocks soaDev1(queue, elementsTrackingHitsLayout, elementsHitModulesLayout);
    TrackingRecHitsSoACollectionBlocks soaDev2(queue, elementsTrackingHitsLayout, elementsHitModulesLayout);

    alpaka::memcpy(queue, soaDev1.buffer(), soaHost1.buffer());
    alpaka::memcpy(queue, soaDev2.buffer(), soaHost2.buffer());

    // soaDev1.updateFromDevice(queue);
    // soaDev2.updateFromDevice(queue);

    alpaka::wait(queue);

    event.emplace(soa1_, std::move(soaDev1));
    event.emplace(soa2_, std::move(soaDev2));

    std::cout << "SoAProducer finished " << std::endl;
  }

private:
  const device::EDPutToken<TrackingRecHitsSoACollectionBlocks> soa1_;
  const device::EDPutToken<TrackingRecHitsSoACollectionBlocks> soa2_;
};

}

DEFINE_FWK_ALPAKA_MODULE(SoAProducerTracking);
