#include <algorithm>
#include <cstdint>
#include <iostream>
#include <vector>

#include <cuda_runtime.h>

#include "DataFormats/FEDRawData/interface/FEDRawData.h"
#include "DataFormats/FEDRawData/interface/FEDRawDataCollection.h"
#include "FWCore/Framework/interface/Event.h"
#include "FWCore/Framework/interface/Frameworkfwd.h"
#include "FWCore/Framework/interface/global/EDAnalyzer.h"
#include "FWCore/ParameterSet/interface/ConfigurationDescriptions.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/ParameterSet/interface/ParameterSetDescription.h"
#include "FWCore/Utilities/interface/InputTag.h"
#include "HeterogeneousCore/CUDAUtilities/interface/cudaCheck.h"

#include "HeterogeneousTest/CUDADevice/interface/GDSPixelGather.h"
#include "HeterogeneousTest/CUDADevice/interface/GDSRawDataDeviceRef.h"

using namespace cms::cuda;
#define cudaCheck(ARG) cms::cuda::cudaCheck(__FILE__, __LINE__, __func__, (ARG))

class GDSPixelGatherAnalyzer : public edm::global::EDAnalyzer<> {
public:
  explicit GDSPixelGatherAnalyzer(edm::ParameterSet const& config)
      : deviceToken_(consumes(config.getParameter<edm::InputTag>("deviceSource"))),
        legacyToken_(consumes(config.getParameter<edm::InputTag>("legacySource"))),
        minFedId_(config.getParameter<uint32_t>("minFedId")),
        maxFedId_(config.getParameter<uint32_t>("maxFedId")),
        validate_(config.getParameter<bool>("validate")) {}

  static void fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
    edm::ParameterSetDescription desc;
    desc.add<edm::InputTag>("deviceSource", edm::InputTag("rawDataCollector"));
    desc.add<edm::InputTag>("legacySource", edm::InputTag("rawDataCollector"));
    desc.add<uint32_t>("minFedId", 1200);  // FEDNumbering::MINSiPixeluTCAFEDID
    desc.add<uint32_t>("maxFedId", 1349);  // FEDNumbering::MAXSiPixeluTCAFEDID
    desc.add<bool>("validate", true);
    descriptions.addWithDefaultLabel(desc);
  }

  void analyze(edm::StreamID, edm::Event const& event, edm::EventSetup const&) const override {
    const gdsraw::RawDataDeviceRef& ref = event.get(deviceToken_);
    gdsgather::GatherResult g = gdsgather::gatherFeds(ref, minFedId_, maxFedId_);

    std::cout << "GDSPixelGather: event " << event.id().event() << "  fragments " << ref.nFeds()
              << "  pixel fragments " << g.nSelected << "  words (== wordCounter) " << g.nWords << std::endl;

    if (validate_ && g.nWords > 0) {
      std::vector<uint32_t> gpuWords(g.nWords);
      cudaCheck(cudaMemcpy(gpuWords.data(), g.d_words, size_t(g.nWords) * sizeof(uint32_t), cudaMemcpyDeviceToHost));

      const FEDRawDataCollection& legacy = event.get(legacyToken_);
      std::vector<uint32_t> cpuWords;
      for (uint32_t fedId = minFedId_; fedId <= maxFedId_; ++fedId) {
        const FEDRawData& d = legacy.FEDData(fedId);
        if (d.size() <= 16)
          continue;
        const uint32_t n = (d.size() - 16) / 4;
        const uint32_t* src = reinterpret_cast<const uint32_t*>(d.data() + 8);
        cpuWords.insert(cpuWords.end(), src, src + n);
      }

      bool ok = (cpuWords.size() == gpuWords.size());
      if (!ok) {
        std::cout << "GDSPixelGather: word count mismatch cpu " << cpuWords.size() << " gpu " << gpuWords.size()
                  << std::endl;
      } else {
        // device visits fragments in chunk order, host loop in fedId order:
        // compare as multisets rather than element by element
        std::sort(cpuWords.begin(), cpuWords.end());
        std::sort(gpuWords.begin(), gpuWords.end());
        ok = (cpuWords == gpuWords);
        if (!ok)
          std::cout << "GDSPixelGather: word content mismatch" << std::endl;
      }
      std::cout << "GDSPixelGather: validation " << (ok ? "PASSED" : "FAILED") << std::endl;
    }

    cudaFree(g.d_words);
    cudaFree(g.d_fedIndex);
  }

private:
  const edm::EDGetTokenT<gdsraw::RawDataDeviceRef> deviceToken_;
  const edm::EDGetTokenT<FEDRawDataCollection> legacyToken_;
  const uint32_t minFedId_, maxFedId_;
  const bool validate_;
};

#include "FWCore/Framework/interface/MakerMacros.h"
DEFINE_FWK_MODULE(GDSPixelGatherAnalyzer);

