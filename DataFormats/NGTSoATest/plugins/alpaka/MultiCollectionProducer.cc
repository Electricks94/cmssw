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

#include "DataFormats/NGTSoATest/interface/HostCollectionSoATest.h"
#include "DataFormats/NGTSoATest/interface/alpaka/DeviceCollectionSoATest.h"

#include <iostream>

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

    auto manager = DeviceCollectionManager(edm::RefProd<SoADeviceCollection>(&soaInputHandle1),
                                           edm::RefProd<SoADeviceCollection>(&soaInputHandle2),
                                           edm::RefProd<SoADeviceCollection>(&soaInputHandle3));

    // Move the SoA Collection manager into the Event.
    event.emplace(outputToken_, std::move(manager));

    std::cout << "Producer 2 finished: I produced a MultiCollectionManager!" << std::endl;

  }

private:
  const device::EDGetToken<SoADeviceCollection> inputToken1_;
  const device::EDGetToken<SoADeviceCollection> inputToken2_;
  const device::EDGetToken<SoADeviceCollection> inputToken3_;

  const device::EDPutToken<DeviceCollectionManager> outputToken_;
};

}

DEFINE_FWK_ALPAKA_MODULE(MultiCollectionProducer);
