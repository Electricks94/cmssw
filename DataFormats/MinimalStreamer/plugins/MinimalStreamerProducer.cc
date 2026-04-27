#include "FWCore/Framework/interface/Event.h"
#include "FWCore/Framework/interface/MakerMacros.h"
#include "FWCore/Framework/interface/global/EDProducer.h"

#include "DataFormats/MinimalStreamer/interface/MinimalStreamerSoA.h"
#include "DataFormats/MinimalStreamer/interface/MinimalStreamerHostCollection.h"


class MinimalStreamerProducer : public edm::global::EDProducer<> {
public:
  explicit MinimalStreamerProducer(const edm::ParameterSet&);
  ~MinimalStreamerProducer() override;

  void produce(edm::StreamID, edm::Event&, const edm::EventSetup&) const override;

private:
};

MinimalStreamerProducer::MinimalStreamerProducer(const edm::ParameterSet& iConfig) {
  produces<MinimalStreamerHostCollection>("TestProduct");
}

MinimalStreamerProducer::~MinimalStreamerProducer() {}

DEFINE_FWK_MODULE(MinimalStreamerProducer);

void MinimalStreamerProducer::produce(edm::StreamID iID, edm::Event& event, const edm::EventSetup& iSetup) const {
  auto product = std::make_unique<MinimalStreamerHostCollection>(cms::alpakatools::host(), 10);

  auto& view = product->view();

  for (int i = 0; i < view.metadata().size(); ++i) {
    view.x()[i] = 1.0f;
    view.y()[i] = 2.0f;
    view.z()[i] = 3.0f;
  }


  event.put(std::move(product), "TestProduct");
  printf("MinimalStreamerProducer finished successfully\n");
}