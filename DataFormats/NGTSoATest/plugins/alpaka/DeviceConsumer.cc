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
      auto queue = event.queue();
      auto device = event.device();

      auto manager = event.get(collectionManagerInputToken_);
      auto view = manager.makeFlatView();
      const auto totalNumberElements = view.size();

      alpaka_common::Vec<alpaka_common::Dim1D> const extent{totalNumberElements};
      auto bufHost{alpaka::allocBuf<float, alpaka_common::Idx>(cms::alpakatools::host(), extent)};
      auto bufAcc{alpaka::allocBuf<float, alpaka_common::Idx>(device, extent)};
      float* h_result{std::data(bufHost)};
      float* d_result{std::data(bufAcc)};

      TestKernel::run(queue, manager, d_result);

      alpaka::wait(queue);
      alpaka::memcpy(queue, bufHost, bufAcc);
      alpaka::wait(queue);

      for (std::size_t i = 0; i < totalNumberElements; ++i) {
        const int result = static_cast<int>(h_result[i]);
        std::cout << result << std::endl;
      }
      
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
