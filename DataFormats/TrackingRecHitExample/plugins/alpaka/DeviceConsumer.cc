#include <alpaka/alpaka.hpp>

#include "FWCore/ParameterSet/interface/ConfigurationDescriptions.h"
#include "FWCore/Utilities/interface/EDGetToken.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/ParameterSet/interface/ParameterSetDescription.h"
#include "FWCore/Utilities/interface/InputTag.h"
#include "FWCore/Utilities/interface/StreamID.h"

#include "DataFormats/TrackingRecHitExample/interface/TrackingRecHitsSoA.h"
#include "DataFormats/TrackingRecHitExample/interface/TrackingRecHitsHost.h"
#include "DataFormats/TrackingRecHitExample/interface/TrackingRecHitsDevice.h"
#include "DataFormats/TrackingRecHitExample/interface/alpaka/TrackingRecHitsSoACollection.h"

#include "HeterogeneousCore/AlpakaCore/interface/alpaka/global/EDProducer.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/Event.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/EventSetup.h"
#include "HeterogeneousCore/AlpakaInterface/interface/config.h"
#include "HeterogeneousCore/AlpakaInterface/interface/workdivision.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/MakerMacros.h"
#include <iostream>

#include "TestKernel.h"

using namespace reco;
using namespace ALPAKA_ACCELERATOR_NAMESPACE::reco;

namespace ALPAKA_ACCELERATOR_NAMESPACE {

  class DeviceConsumerTracking : public global::EDProducer<> {
  public:

    DeviceConsumerTracking(edm::ParameterSet const& config)
    : EDProducer<>(config), 
      multiCollectionInputToken_(consumes(config.getParameter<edm::InputTag>("multiCollectionInput"))) {}

    void produce(edm::StreamID sid, device::Event& event, device::EventSetup const& setup) const override {
      auto queue = event.queue();
      auto device = event.device();

      auto multiCollection = event.get(multiCollectionInputToken_);

      // std::cout << "nHits: " << manager.nHits() << std::endl;
      // std::cout << "nModules: " << manager.nModules() << std::endl;
      // std::cout << "offsetBPIX2: " << manager.offsetBPIX2() << std::endl;

      auto view = multiCollection.makeFlatView();
      const auto totalNumberElements = view.TrackingHits.size();

      std::cout << "view.TrackingHits.size(): " << totalNumberElements << std::endl;

      /*
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
      */
      
    }

    static void fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
      edm::ParameterSetDescription desc;
      desc.add<edm::InputTag>("multiCollectionInput");
      descriptions.addWithDefaultLabel(desc);
    }

  private:
    const edm::EDGetTokenT<TrackingRecHitsMultiCollection> multiCollectionInputToken_;
  };
}  // namespace ALPAKA_ACCELERATOR_NAMESPACE

DEFINE_FWK_ALPAKA_MODULE(DeviceConsumerTracking);
