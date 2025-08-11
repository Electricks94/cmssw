#include "CommonTools/RecoAlgos/interface/MultiCollectionManager.h"
#include "DataFormats/NGTSoATest/interface/HostCollectionSoATest.h"

#include "FWCore/MessageLogger/interface/MessageLogger.h"
#include "FWCore/Framework/interface/Event.h"
#include "FWCore/Framework/interface/MakerMacros.h"
#include "FWCore/Framework/interface/global/EDProducer.h"

#include <iostream>

class MultiCollectionConsumer : public edm::global::EDProducer<> {
public:
  explicit MultiCollectionConsumer(const edm::ParameterSet&);
  ~MultiCollectionConsumer();

  void produce(edm::StreamID, edm::Event&, const edm::EventSetup&) const override;

private:
  edm::EDGetTokenT<CollectionManager> collectionManagerInputToken_;
};

MultiCollectionConsumer::MultiCollectionConsumer(const edm::ParameterSet& iConfig)
    : collectionManagerInputToken_(consumes<CollectionManager>(iConfig.getParameter<edm::InputTag>("collectionManagerInput"))) {
}

MultiCollectionConsumer::~MultiCollectionConsumer() {}

DEFINE_FWK_MODULE(MultiCollectionConsumer);

void MultiCollectionConsumer::produce(edm::StreamID iID, edm::Event& event, const edm::EventSetup& iSetup) const {

  auto const& manager = event.getHandle(collectionManagerInputToken_);
  
  auto view = manager->makeFlatView();  // by value

  std::cout << "Size of Manager: " << view.size() << std::endl;

  // const auto test = view.getView(0);
  // std::cout << "Size of View 0: " << test.metadata().size() << std::endl;
  for (unsigned int i = 0; i < view.size(); ++i) {
     std::cout << "Consumer x0: " << view[i].x0() << std::endl;
  }

  std::cout << "Producer 3 finished: I consumed a MultiCollectionManager!" << std::endl;
}