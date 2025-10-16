// user include files
#include <unordered_map>

#include "FWCore/Framework/interface/Frameworkfwd.h"
#include "FWCore/Framework/interface/global/EDProducer.h"
#include "FWCore/MessageLogger/interface/MessageLogger.h"

#include "FWCore/Framework/interface/Event.h"
#include "FWCore/Framework/interface/ESHandle.h"
#include "FWCore/Framework/interface/MakerMacros.h"
#include "FWCore/ParameterSet/interface/ConfigurationDescriptions.h"
#include "FWCore/ParameterSet/interface/ParameterSetDescription.h"

#include "DataFormats/HGCRecHit/interface/HGCRecHitCollections.h"
#include "DataFormats/ParticleFlowReco/interface/PFRecHit.h"
#include "DataFormats/Common/interface/MultiCollection.h"
#include "DataFormats/Common/interface/MultiSpan.h"

class RecHitMapProducer : public edm::global::EDProducer<> {
public:
  RecHitMapProducer(const edm::ParameterSet&);
  static void fillDescriptions(edm::ConfigurationDescriptions& descriptions);

  void produce(edm::StreamID, edm::Event&, const edm::EventSetup&) const override;

private:
  const edm::EDGetTokenT<edm::MultiCollection<HGCRecHitCollection>> hgcalToken_;
  const edm::EDGetTokenT<edm::MultiCollection<reco::PFRecHitCollection>> barrelToken_;

  bool hgcalOnly_;
};

DEFINE_FWK_MODULE(RecHitMapProducer);

using DetIdRecHitMap = std::unordered_map<DetId, const unsigned int>;

RecHitMapProducer::RecHitMapProducer(const edm::ParameterSet& ps)
    : hgcalToken_{consumes<edm::MultiCollection<HGCRecHitCollection>>(
          ps.getParameter<edm::InputTag>("HGCalMultiRecHits"))},
      barrelToken_{
          consumes<edm::MultiCollection<reco::PFRecHitCollection>>(ps.getParameter<edm::InputTag>("HGCalBarrelHits"))},
      hgcalOnly_(ps.getParameter<bool>("hgcalOnly")) {
  produces<DetIdRecHitMap>("hgcalRecHitMap");
  if (!hgcalOnly_)
    produces<DetIdRecHitMap>("barrelRecHitMap");
}

void RecHitMapProducer::fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
  edm::ParameterSetDescription desc;
  desc.add<edm::InputTag>("HGCalMultiRecHits", {"hgcalRecHitMultiCollectionProducer", ""});
  desc.add<edm::InputTag>("HGCalBarrelHits", {"hgcalRecHitMultiCollectionProducer", ""});
  desc.add<bool>("hgcalOnly", true);
  descriptions.add("recHitMapProducer", desc);
}

void RecHitMapProducer::produce(edm::StreamID, edm::Event& evt, const edm::EventSetup& es) const {
  auto hitMapHGCal = std::make_unique<DetIdRecHitMap>();

  // Retrieve HGCalMultiCollection
  auto const& mcHGCRecHit = evt.get(hgcalToken_);
  auto HGCRecHitFlat = mcHGCRecHit.makeFlatView();
  if (HGCRecHitFlat.size() > 0) {
    for (unsigned int i = 0; i < HGCRecHitFlat.size(); ++i) {
      hitMapHGCal->emplace(HGCRecHitFlat[i].detid(), i);
    }
  } else {
    edm::LogWarning("RecHitMapProducer") << "HGCal MultiCollection is empty. Returning an empty map.";
  }
  evt.put(std::move(hitMapHGCal), "hgcalRecHitMap");

  if (!hgcalOnly_) {
    auto hitMapBarrel = std::make_unique<DetIdRecHitMap>();
    // Retrieve Barrel MultiCollection
    auto const& mcPFRecHit = evt.get(barrelToken_);
    auto PFRecHitFlat = mcPFRecHit.makeFlatView();
    if (PFRecHitFlat.size() > 0) {
      for (unsigned int i = 0; i < PFRecHitFlat.size(); ++i) {
        hitMapBarrel->emplace(PFRecHitFlat[i].detId(), i);
      }
    } else {
      edm::LogWarning("RecHitMapProducer") << "Barrel hit MultiCollection is empty. Returning an empty map.";
    }
    evt.put(std::move(hitMapBarrel), "barrelRecHitMap");
  }
}
