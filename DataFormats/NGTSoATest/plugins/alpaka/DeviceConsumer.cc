#include <alpaka/alpaka.hpp>

#include "FWCore/ParameterSet/interface/ConfigurationDescriptions.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/ParameterSet/interface/ParameterSetDescription.h"
#include "FWCore/Utilities/interface/InputTag.h"
#include "FWCore/Utilities/interface/StreamID.h"

#include "DataFormats/NGTSoATest/interface/HostCollectionSoATest.h"
#include "DataFormats/NGTSoATest/interface/alpaka/DeviceCollectionSoATest.h"

#include "HeterogeneousCore/AlpakaCore/interface/alpaka/global/EDProducer.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/Event.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/EventSetup.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/EDGetToken.h"
#include "HeterogeneousCore/AlpakaInterface/interface/config.h"
#include "HeterogeneousCore/AlpakaInterface/interface/workdivision.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/MakerMacros.h"
#include <iostream>

#include "TestKernel.h"

namespace ALPAKA_ACCELERATOR_NAMESPACE {

  class DeviceConsumer : public global::EDProducer<> {
  public:

    DeviceConsumer(edm::ParameterSet const& config)
    : EDProducer<>(config), 
      collectionManagerInputToken_(consumes(config.getParameter<edm::InputTag>("collectionManagerInput"))) {}

    void produce(edm::StreamID sid, device::Event& event, device::EventSetup const& setup) const override {
      
      DeviceCollectionManager manager = event.get(collectionManagerInputToken_);
      TestKernel::run(event.queue(), manager);

      std::cout << "DeviceConsumer worked: " << std::endl;
    }

    static void fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
      edm::ParameterSetDescription desc;
      desc.add<edm::InputTag>("collectionManagerInput");
      descriptions.addWithDefaultLabel(desc);
    }

  private:
    const device::EDGetToken<DeviceCollectionManager> collectionManagerInputToken_;
  };
}  // namespace ALPAKA_ACCELERATOR_NAMESPACE

DEFINE_FWK_ALPAKA_MODULE(DeviceConsumer);
