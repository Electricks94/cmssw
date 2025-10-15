// Author: Felice Pantaleo (CERN), 2025, felice.pantaleo@cern.ch
//
// Produce a MultiCollection<HGCRecHitCollection> that bundles the EE, FH
// and BH rec‑hit branches into one flat, persistent object.  Analysis modules
// such as RecHitMapProducer can consume the manager and access the hits via the
// usual MultiSpan API.
//
// Configuration example:
// cms.EDProducer("HGCalRecHitMultiCollectionProducer",
//                EEInput = cms.InputTag("HGCalRecHit", "HGCEERecHits"),
//                FHInput = cms.InputTag("HGCalRecHit", "HGCHEFRecHits"),
//                BHInput = cms.InputTag("HGCalRecHit", "HGCHEBRecHits"))

#include "FWCore/Framework/interface/global/EDProducer.h"
#include "FWCore/Framework/interface/MakerMacros.h"
#include "FWCore/Framework/interface/Event.h"
#include "FWCore/Framework/interface/Frameworkfwd.h"
#include "FWCore/MessageLogger/interface/MessageLogger.h"
#include "FWCore/ParameterSet/interface/ConfigurationDescriptions.h"
#include "FWCore/ParameterSet/interface/ParameterSetDescription.h"

#include "DataFormats/Common/interface/MultiCollection.h"
#include "DataFormats/ParticleFlowReco/interface/PFRecHit.h"
#include "DataFormats/HGCRecHit/interface/HGCRecHitCollections.h"

class HGCalRecHitMultiCollectionProducer : public edm::global::EDProducer<> {
public:
  explicit HGCalRecHitMultiCollectionProducer(edm::ParameterSet const& ps) {
    std::vector<edm::InputTag> tags = ps.getParameter<std::vector<edm::InputTag>>("hits");
    for (auto& tag : tags) {
      if (tag.label().find("HGCalRecHit") != std::string::npos) {
        hgcal_hits_token_.push_back(consumes<HGCRecHitCollection>(tag));
      } else {
        barrel_hits_token_.push_back(consumes<reco::PFRecHitCollection>(tag));
      }
    }

    produces<edm::MultiCollection<HGCRecHitCollection>>();
    produces<edm::MultiCollection<reco::PFRecHitCollection>>();
  }

  static void fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
    edm::ParameterSetDescription desc;
    desc.add<std::vector<edm::InputTag>>("hits",
                                         {edm::InputTag("HGCalRecHit", "HGCEERecHits"),
                                          edm::InputTag("HGCalRecHit", "HGCHEFRecHits"),
                                          edm::InputTag("HGCalRecHit", "HGCHEBRecHits")});
    descriptions.add("hgcalRecHitMultiCollectionProducer", desc);
  }

  void produce(edm::StreamID, edm::Event& evt, edm::EventSetup const&) const override {
    // Retrieve collections
    const auto& ee_hits = evt.getHandle(hgcal_hits_token_[0]);
    const auto& fh_hits = evt.getHandle(hgcal_hits_token_[1]);
    const auto& bh_hits = evt.getHandle(hgcal_hits_token_[2]);

    // Check validity of all handles
    if ((ee_hits.isValid()) && (fh_hits.isValid()) && (bh_hits.isValid())) {
      auto mcHGCRecHit = std::make_unique<edm::MultiCollection<HGCRecHitCollection>>();
      mcHGCRecHit->add(edm::RefProd<HGCRecHitCollection>(ee_hits));
      mcHGCRecHit->add(edm::RefProd<HGCRecHitCollection>(fh_hits));
      mcHGCRecHit->add(edm::RefProd<HGCRecHitCollection>(bh_hits));
      evt.put(std::move(mcHGCRecHit));
    } else {
      edm::LogWarning("HGCalRecHitMultiCollectionProducer")
          << "At least one HGCal rechit collection is missing. Producing an empty "
             "MultiCollection<HGCRecHitCollection>.";
      evt.put(std::make_unique<edm::MultiCollection<HGCRecHitCollection>>());
    }

    // Retrieve collections
    const auto& ecal_hits = evt.getHandle(barrel_hits_token_[0]);
    const auto& hbhe_hits = evt.getHandle(barrel_hits_token_[1]);

    if ((ecal_hits.isValid()) && (hbhe_hits.isValid())) {
      auto mcPFRecHit = std::make_unique<edm::MultiCollection<reco::PFRecHitCollection>>();
      mcPFRecHit->add(edm::RefProd<reco::PFRecHitCollection>(ecal_hits));
      mcPFRecHit->add(edm::RefProd<reco::PFRecHitCollection>(hbhe_hits));
      evt.put(std::move(mcPFRecHit));
    } else {
      edm::LogWarning("HGCalRecHitMultiCollectionProducer")
          << "One or more barrel hit collections are unavailable. Producing an empty "
             "MultiCollection<reco::PFRecHitCollection>.";
      evt.put(std::make_unique<edm::MultiCollection<reco::PFRecHitCollection>>());
    }
  }

private:
  std::vector<edm::EDGetTokenT<HGCRecHitCollection>> hgcal_hits_token_;
  std::vector<edm::EDGetTokenT<reco::PFRecHitCollection>> barrel_hits_token_;
};

DEFINE_FWK_MODULE(HGCalRecHitMultiCollectionProducer);
