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

#include "DataFormats/TrackingRecHitExample/interface/HostCollectionSoATest.h"
#include "DataFormats/TrackingRecHitExample/interface/alpaka/DeviceCollectionSoATest.h"

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
    
    const cms::soa::size_type elements1 = 1;
    const cms::soa::size_type elements2 = 2;

    TrackingRecHitHost soaHost1(cms::alpakatools::host(), elements1, elements1);
    TrackingRecHitHost soaHost2(cms::alpakatools::host(), elements2, elements2);

    auto& view1 = soaHost1.view<TrackingRecHitSoA>();
    auto& view2 = soaHost2.view<TrackingRecHitSoA>();

    view1.offsetBPIX2() = 100;
    view2.offsetBPIX2() = 200;

    for (TrackingRecHitView::size_type i = 0; i < view1.metadata().size(); ++i) {
      view1[i].xLocal() = 1.0f;
    }

    for (TrackingRecHitView::size_type i = 0; i < view2.metadata().size(); ++i) {
      view2[i].xLocal() = 2.0f;
    }

    TrackingRecHitsSoACollection soaDev1(queue, elements1, elements1);
    TrackingRecHitsSoACollection soaDev2(queue, elements2, elements2);

    alpaka::memcpy(queue, soaDev1.buffer(), soaHost1.buffer());
    alpaka::memcpy(queue, soaDev2.buffer(), soaHost2.buffer());

    soaDev1.updateFromDevice(queue);
    soaDev2.updateFromDevice(queue);

    alpaka::wait(queue);

    event.emplace(soa1_, std::move(soaDev1));
    event.emplace(soa2_, std::move(soaDev2));

    std::cout << "SoAProducer finished " << std::endl;
  }

private:
  const device::EDPutToken<TrackingRecHitsSoACollection> soa1_;
  const device::EDPutToken<TrackingRecHitsSoACollection> soa2_;
};

}

DEFINE_FWK_ALPAKA_MODULE(SoAProducerTracking);
