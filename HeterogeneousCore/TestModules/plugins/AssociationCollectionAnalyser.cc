#include "FWCore/Framework/interface/Event.h"
#include "FWCore/Framework/interface/EventSetup.h"
#include "FWCore/Framework/interface/Frameworkfwd.h"
#include "FWCore/Framework/interface/global/EDAnalyzer.h"
#include "FWCore/MessageLogger/interface/MessageLogger.h"
#include "FWCore/ParameterSet/interface/ConfigurationDescriptions.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/ParameterSet/interface/ParameterSetDescription.h"
#include "FWCore/Utilities/interface/EDGetToken.h"
#include "FWCore/Utilities/interface/InputTag.h"

#include "DataFormats/TICL/interface/FillAssociator.h"
#include "HeterogeneousCore/TestModules/interface/AssociationCollection.h"

#include <ranges>


class AssociationCollectionAnalyzer : public edm::global::EDAnalyzer<> {
public:
  AssociationCollectionAnalyzer(edm::ParameterSet const& config)
      : source_{config.getParameter<edm::InputTag>("source")}, associationCollectionToken_{consumes(source_)} {}

  void analyze(edm::StreamID sid, edm::Event const& event, edm::EventSetup const&) const override {
    AssociationCollection const& map = event.get(associationCollectionToken_);

    const int nkeys = 2;
    //const int nvalues = 100u;


    for(int i = 0; i < nkeys + 1; ++i) {
       std::cout << "map->view().offsets()[" << i << "]: " << map.view().offsets().keys_offsets()[i] << std::endl;
    }

      // for(int i = 0; i < nvalues; ++i) {
      //   map->view().content()[i] = static_cast<uint32_t>(i);
      // }

    std::cout << "Analysing AssociationCollection with " << map.view().metadata().size()[1] << " keys and " << map.view().metadata().size()[0] << " values." << std::endl;




    std::cout << "Initialising new collection from the size of the old one ..." << std::endl;

    AssociationCollection newMap(cms::alpakatools::host(), map.size());

    std::cout << "New Collection sizes: " << newMap.view().metadata().size()[1] << " keys and " << newMap.view().metadata().size()[0] << " values." << std::endl;

  }

  static void fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
    edm::ParameterSetDescription desc;
    desc.add<edm::InputTag>("source");
  }

private:
  const edm::InputTag source_;
  const edm::EDGetTokenT<AssociationCollection> associationCollectionToken_;
};

#include "FWCore/Framework/interface/MakerMacros.h"
DEFINE_FWK_MODULE(AssociationCollectionAnalyzer);