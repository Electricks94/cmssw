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

class MultiCollectionProducerTracking : public global::EDProducer<> {
public:
  MultiCollectionProducerTracking(edm::ParameterSet const& config)
    : EDProducer<>(config),
      inputToken1_(consumes(config.getParameter<edm::InputTag>("soaInput1"))),
      inputToken2_(consumes(config.getParameter<edm::InputTag>("soaInput2"))),
      outputToken_(produces()) {}

  static void fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
    edm::ParameterSetDescription desc;
    desc.add<edm::InputTag>("soaInput1");
    desc.add<edm::InputTag>("soaInput2");
    descriptions.addWithDefaultLabel(desc);
  }

  void produce(edm::StreamID sid, device::Event& event, device::EventSetup const& setup) const override{

    auto const& soaInputHandle1 = event.get(inputToken1_);
    auto const& soaInputHandle2 = event.get(inputToken2_);

    auto manager = DeviceCollectionManagerTracking(edm::RefProd<TrackingRecHitsSoACollection>(&soaInputHandle1),
                                                   edm::RefProd<TrackingRecHitsSoACollection>(&soaInputHandle2));
                                             
    // Move the SoA Collection manager into the Event.
    event.emplace(outputToken_, std::move(manager));

    std::cout << "DeviceCollectionManagerTracking produced " << std::endl;
  }

private:
  const device::EDGetToken<TrackingRecHitsSoACollection> inputToken1_;
  const device::EDGetToken<TrackingRecHitsSoACollection> inputToken2_;

  const device::EDPutToken<DeviceCollectionManagerTracking> outputToken_;
};

}

DEFINE_FWK_ALPAKA_MODULE(MultiCollectionProducerTracking);