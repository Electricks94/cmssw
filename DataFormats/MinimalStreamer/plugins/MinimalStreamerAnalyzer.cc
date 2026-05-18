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

#include "DataFormats/MinimalStreamer/interface/MinimalStreamerSoA.h"
#include "DataFormats/MinimalStreamer/interface/MinimalStreamerHostCollection.h"

class MinimalStreamerAnalyzer : public edm::global::EDAnalyzer<> {
public:
  MinimalStreamerAnalyzer(edm::ParameterSet const& config)
      : source_{config.getParameter<edm::InputTag>("source")}, soaToken_{consumes(source_)} {}

  void analyze(edm::StreamID sid, edm::Event const& event, edm::EventSetup const&) const override {
    MinimalStreamerHostCollection const& product = event.get(soaToken_);
    auto const& view = product.const_view();

    for (int i = 0; i < view.metadata().size(); ++i) {
      std::cout << "MinimalStreamerAnalyzer idx: " << i
                << ", x " << view.x()[i] << ", y " << view.y()[i] << ", z " << view.z()[i] << std::endl;
    }

    std::cout << "MinimalStreamerAnalyzer scalars: s1 " << view.s1() << ", s2 " << view.s2() << ", s3 " << view.s3()
              << std::endl;
  }

  static void fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
    edm::ParameterSetDescription desc;
    desc.add<edm::InputTag>("source");
  }

private:
  const edm::InputTag source_;
  const edm::EDGetTokenT<MinimalStreamerHostCollection> soaToken_;
};

#include "FWCore/Framework/interface/MakerMacros.h"
DEFINE_FWK_MODULE(MinimalStreamerAnalyzer);