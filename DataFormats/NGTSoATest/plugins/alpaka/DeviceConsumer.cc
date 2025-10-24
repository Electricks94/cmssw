#include <alpaka/alpaka.hpp>

#include "FWCore/ParameterSet/interface/ConfigurationDescriptions.h"
#include "FWCore/Utilities/interface/EDGetToken.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/ParameterSet/interface/ParameterSetDescription.h"
#include "FWCore/Utilities/interface/InputTag.h"
#include "FWCore/Utilities/interface/StreamID.h"

#include "DataFormats/Portable/interface/MultiView.h"
#include "DataFormats/NGTSoATest/interface/HostCollectionSoATest.h"
#include "DataFormats/NGTSoATest/interface/alpaka/DeviceCollectionSoATest.h"

#include "HeterogeneousCore/AlpakaCore/interface/alpaka/global/EDProducer.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/Event.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/EventSetup.h"
#include "HeterogeneousCore/AlpakaInterface/interface/config.h"
#include "HeterogeneousCore/AlpakaInterface/interface/workdivision.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/MakerMacros.h"

#include <ranges>
#include <iostream>

#include "TestKernel.h"

namespace ALPAKA_ACCELERATOR_NAMESPACE {

  class DeviceConsumer : public global::EDProducer<> {
  public:

    DeviceConsumer(edm::ParameterSet const& config)
    : EDProducer<>(config), 
      multiCollectionInputToken_(consumes(config.getParameter<edm::InputTag>("multiCollectionInput"))) {}

    void produce(edm::StreamID sid, device::Event& event, device::EventSetup const& setup) const override {
      auto queue = event.queue();
      auto device = event.device();

      auto mc = event.get(multiCollectionInputToken_);

      MultiView<SoA, 3> mv;
      for(auto const& rp : mc) {
        mv.addView(rp->const_view());
      }

      const auto totalNumberElements = mv.size();

      // GetScalar<ConstSoAView> getX2; 
      // auto sum = view.getScalar(getX2, [](auto a, auto b) { return a + b; }, 0);
      // std::cout << "SCALAR x2 values from all SoAs in MultiCollection: " <<  sum << std::endl;

      alpaka_common::Vec<alpaka_common::Dim1D> const extent{totalNumberElements};
      auto bufHost{alpaka::allocBuf<float, alpaka_common::Idx>(cms::alpakatools::host(), extent)};
      auto bufAcc{alpaka::allocBuf<float, alpaka_common::Idx>(device, extent)};
      float* h_result{std::data(bufHost)};
      float* d_result{std::data(bufAcc)};

      TestKernel::run(queue, mv, d_result);

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
      desc.add<edm::InputTag>("multiCollectionInput");
      descriptions.addWithDefaultLabel(desc);
    }

  private:
    const edm::EDGetTokenT<MultiCollection> multiCollectionInputToken_;
  };
}  // namespace ALPAKA_ACCELERATOR_NAMESPACE

DEFINE_FWK_ALPAKA_MODULE(DeviceConsumer);
