#include "FWCore/Framework/interface/Event.h"
#include "FWCore/Framework/interface/MakerMacros.h"
#include "FWCore/Framework/interface/global/EDProducer.h"

#include "DataFormats/TICL/interface/FillAssociator.h"
#include "HeterogeneousCore/TestModules/interface/AssociationCollection.h"

#include <ranges>


class AssociationCollectionProducer : public edm::global::EDProducer<> {
public:
  explicit AssociationCollectionProducer(const edm::ParameterSet&);
  ~AssociationCollectionProducer() override;

  void produce(edm::StreamID, edm::Event&, const edm::EventSetup&) const override;

private:
};

AssociationCollectionProducer::AssociationCollectionProducer(const edm::ParameterSet& iConfig) {
  produces<AssociationCollection>("AssociationCollection");
}

AssociationCollectionProducer::~AssociationCollectionProducer() {}

DEFINE_FWK_MODULE(AssociationCollectionProducer);

void AssociationCollectionProducer::produce(edm::StreamID iID, edm::Event& event, const edm::EventSetup& iSetup) const {
      const int nkeys = 2;
      const int nvalues = 100;
      auto map = std::make_unique<AssociationCollection>(cms::alpakatools::host(), nvalues, nkeys);

      for(int i = 0; i < nkeys + 1; ++i) {
        map->view().offsets().keys_offsets()[i] = static_cast<uint32_t>(i);
      }

      for(int i = 0; i < nvalues; ++i) {
        map->view().content().values()[i] = static_cast<uint32_t>(i);
      }

      std::cout << "map->view().keys(): " << map->view().keys() << std::endl;
      std::cout << "map->view().size(): " << map->view().size() << std::endl;


    std::cout << "Producing AssociationCollection with " << map->view().metadata().size()[1] << " keys and " << map->view().metadata().size()[0] << " values." << std::endl;

  event.put(std::move(map), "AssociationCollection");
}
