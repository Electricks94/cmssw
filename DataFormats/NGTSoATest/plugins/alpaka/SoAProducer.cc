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

class SoAProducer : public global::EDProducer<> {
public:
  SoAProducer(edm::ParameterSet const& config)
      : EDProducer<>(config), soa1_{produces("SoAProduct1")}, soa2_{produces("SoAProduct2")}, soa3_{produces("SoAProduct3")} {}

  ~SoAProducer(){};

  static void fillDescriptions(edm::ConfigurationDescriptions& descriptions) {}

  void produce(edm::StreamID sid, device::Event& event, device::EventSetup const&) const override {
    auto queue = event.queue();
    
    const cms::soa::size_type elements1 = 5;
    const cms::soa::size_type elements2 = 25;
    const cms::soa::size_type elements3 = 13;

    SoAHostCollection soaHost1(elements1, cms::alpakatools::host());
    SoAHostCollection soaHost2(elements2, cms::alpakatools::host());
    SoAHostCollection soaHost3(elements3, cms::alpakatools::host());

    auto& view1 = soaHost1.view();
    auto& view2 = soaHost2.view();
    auto& view3 = soaHost3.view();

    view1.x2() = static_cast<int>(elements1);
    view2.x2() = static_cast<int>(elements2);
    view3.x2() = static_cast<int>(elements3);

    for (SoAView::size_type i = 0; i < view1.metadata().size(); ++i) {
      view1[i] = {1.0f, {2.0f, 3.0f, 4.0f}};
    }

    for (SoAView::size_type i = 0; i < view2.metadata().size(); ++i) {
      view2[i] = {5.0f, {6.0f, 7.0f, 8.0f}};
    }

    for (SoAView::size_type i = 0; i < view3.metadata().size(); ++i) {
      view3[i] = {9.0f, {10.0f, 11.0f, 12.0f}};
    }

    SoADeviceCollection soaDev1(elements1, queue);
    SoADeviceCollection soaDev2(elements2, queue);
    SoADeviceCollection soaDev3(elements3, queue);

    alpaka::memcpy(queue, soaDev1.buffer(), soaHost1.buffer());
    alpaka::memcpy(queue, soaDev2.buffer(), soaHost2.buffer());
    alpaka::memcpy(queue, soaDev3.buffer(), soaHost3.buffer());

    alpaka::wait(queue);

    event.emplace(soa1_, std::move(soaDev1));
    event.emplace(soa2_, std::move(soaDev2));
    event.emplace(soa3_, std::move(soaDev3));
  }

private:
  const device::EDPutToken<SoADeviceCollection> soa1_;
  const device::EDPutToken<SoADeviceCollection> soa2_;
  const device::EDPutToken<SoADeviceCollection> soa3_;
};

}

DEFINE_FWK_ALPAKA_MODULE(SoAProducer);
