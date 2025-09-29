#include "FWCore/ParameterSet/interface/ConfigurationDescriptions.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/ParameterSet/interface/ParameterSetDescription.h"
#include "FWCore/Utilities/interface/InputTag.h"
#include "FWCore/Utilities/interface/StreamID.h"
#include "FWCore/Utilities/interface/EDPutToken.h"

#include "HeterogeneousCore/AlpakaCore/interface/alpaka/global/EDProducer.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/Event.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/EventSetup.h"
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

    auto multiCollection = TrackingRecHitsMultiCollection();
    multiCollection.addCollection(edm::RefProd<TrackingRecHitsSoACollectionBlocks>(&soaInputHandle1));
    multiCollection.addCollection(edm::RefProd<TrackingRecHitsSoACollectionBlocks>(&soaInputHandle2));                                              
                                             
    // Move the SoA Collection manager into the Event.
    event.emplace(outputToken_, std::move(multiCollection));

    std::cout << "MultiCollection produced " << std::endl;
  }

private:
  const device::EDGetToken<TrackingRecHitsSoACollectionBlocks> inputToken1_;
  const device::EDGetToken<TrackingRecHitsSoACollectionBlocks> inputToken2_;

  const edm::EDPutTokenT<TrackingRecHitsMultiCollection> outputToken_;
};

}

DEFINE_FWK_ALPAKA_MODULE(MultiCollectionProducerTracking);
