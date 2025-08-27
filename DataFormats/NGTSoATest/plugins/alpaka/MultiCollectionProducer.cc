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

#include "DataFormats/NGTSoATest/interface/HostCollectionSoATest.h"
#include "DataFormats/NGTSoATest/interface/alpaka/DeviceCollectionSoATest.h"

// #include <iostream>

namespace ALPAKA_ACCELERATOR_NAMESPACE {

class MultiCollectionProducer : public global::EDProducer<> {
public:
  MultiCollectionProducer(edm::ParameterSet const& config)
    : EDProducer<>(config),
      inputToken1_(consumes(config.getParameter<edm::InputTag>("soaInput1"))),
      inputToken2_(consumes(config.getParameter<edm::InputTag>("soaInput2"))),
      inputToken3_(consumes(config.getParameter<edm::InputTag>("soaInput3"))),
      outputToken_(produces()) {}

  static void fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
    edm::ParameterSetDescription desc;
    desc.add<edm::InputTag>("soaInput1");
    desc.add<edm::InputTag>("soaInput2");
    desc.add<edm::InputTag>("soaInput3");
    descriptions.addWithDefaultLabel(desc);
  }

  void produce(edm::StreamID sid, device::Event& event, device::EventSetup const& setup) const override{

    auto const& soaInputHandle1 = event.get(inputToken1_);
    auto const& soaInputHandle2 = event.get(inputToken2_);
    auto const& soaInputHandle3 = event.get(inputToken3_);

    auto manager = MultiCollectionManager<SoADeviceCollection>();

    manager.addCollection(edm::RefProd<SoADeviceCollection>(&soaInputHandle1));
    manager.addCollection(edm::RefProd<SoADeviceCollection>(&soaInputHandle2));
    manager.addCollection(edm::RefProd<SoADeviceCollection>(&soaInputHandle3));

    //auto view = manager.makeFlatView();

    // Move the SoA Collection manager into the Event.
    event.emplace(outputToken_, std::move(manager));
  }

private:
  const device::EDGetToken<SoADeviceCollection> inputToken1_;
  const device::EDGetToken<SoADeviceCollection> inputToken2_;
  const device::EDGetToken<SoADeviceCollection> inputToken3_;

  const edm::EDPutTokenT<MultiCollectionManager<SoADeviceCollection>> outputToken_;
};

}

DEFINE_FWK_ALPAKA_MODULE(MultiCollectionProducer);
