#include "FWCore/MessageLogger/interface/MessageLogger.h"
#include "FWCore/Framework/interface/Event.h"
#include "FWCore/Framework/interface/MakerMacros.h"
#include "FWCore/Framework/interface/global/EDProducer.h"

#include "DataFormats/NGTSoATest/interface/HostCollectionSoATest.h"
#include <iostream>

class SoAProducer : public edm::global::EDProducer<> {
public:
  explicit SoAProducer(const edm::ParameterSet&);
  ~SoAProducer();

  void produce(edm::StreamID, edm::Event&, const edm::EventSetup&) const override;

private:
};

SoAProducer::SoAProducer(const edm::ParameterSet& iConfig) {
  produces<SoAHostCollection>("SoAProduct1");
  produces<SoAHostCollection>("SoAProduct2");
  produces<SoAHostCollection>("SoAProduct3");
}

SoAProducer::~SoAProducer() {}

DEFINE_FWK_MODULE(SoAProducer);

void SoAProducer::produce(edm::StreamID iID, edm::Event& event, const edm::EventSetup& iSetup) const {
  const cms::soa::size_type elements1 = 5;
  
  const cms::soa::size_type elements2 = 25;
  const cms::soa::size_type elements3 = 13;

  auto SoAProduct1 = std::make_unique<SoAHostCollection>(elements1, cms::alpakatools::host());
  auto SoAProduct2 = std::make_unique<SoAHostCollection>(elements2, cms::alpakatools::host());
  auto SoAProduct3 = std::make_unique<SoAHostCollection>(elements3, cms::alpakatools::host());

  auto& view1 = SoAProduct1->view();
  auto& view2 = SoAProduct2->view();
  auto& view3 = SoAProduct3->view();

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

  event.put(std::move(SoAProduct1), "SoAProduct1");
  event.put(std::move(SoAProduct2), "SoAProduct2");
  event.put(std::move(SoAProduct3), "SoAProduct3");

  std::cout << "Producer 1 finished: I produced three portable collections !" << std::endl;
} 
