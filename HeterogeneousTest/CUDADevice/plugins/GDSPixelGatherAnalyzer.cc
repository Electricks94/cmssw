#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <vector>

#include <cuda_runtime.h>

#include "FWCore/Framework/interface/Event.h"
#include "FWCore/Framework/interface/Frameworkfwd.h"
#include "FWCore/Framework/interface/global/EDAnalyzer.h"
#include "FWCore/ParameterSet/interface/ConfigurationDescriptions.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/ParameterSet/interface/ParameterSetDescription.h"
#include "FWCore/Utilities/interface/InputTag.h"
#include "HeterogeneousCore/CUDAUtilities/interface/cudaCheck.h"

#include "HeterogeneousTest/CUDADevice/interface/GDSPixelDecode.h"
#include "HeterogeneousTest/CUDADevice/interface/GDSPixelGather.h"
#include "HeterogeneousTest/CUDADevice/interface/GDSRawDataDeviceRef.h"

using namespace cms::cuda;
#define cudaCheck(ARG) cms::cuda::cudaCheck(__FILE__, __LINE__, __func__, (ARG))

// Device pipeline stage 2: gather + decode, with timing.
//
// gatherFeds() and decodeWords() both end in cudaDeviceSynchronize(), so plain
// host-side clocks around them measure real GPU work.
//
// The per-stage totals are SUMS OVER EVENTS. With more than one EDM stream those
// events run concurrently, so the sums exceed wall time -- they are a cost
// breakdown, not a duration. Wall time is measured separately.

namespace {
  inline uint64_t nowNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
  }
}  // namespace

class GDSPixelGatherAnalyzer : public edm::global::EDAnalyzer<> {
public:
  explicit GDSPixelGatherAnalyzer(edm::ParameterSet const& config)
      : deviceToken_(consumes(config.getParameter<edm::InputTag>("deviceSource"))),
        minFedId_(config.getParameter<uint32_t>("minFedId")),
        maxFedId_(config.getParameter<uint32_t>("maxFedId")),
        dumpDigis_(config.getParameter<uint32_t>("dumpDigis")),
        printEvery_(config.getParameter<uint32_t>("printEvery")) {}

  ~GDSPixelGatherAnalyzer() override {
    const uint64_t n = nEvents_.load();
    if (n == 0)
      return;
    const double gatherMs = gatherNs_.load() / 1e6;
    const double decodeMs = decodeNs_.load() / 1e6;
    const double wallS = (lastNs_.load() - firstNs_.load()) / 1e9;
    const uint64_t words = totalWords_.load();
    const uint64_t valid = totalValid_.load();

    std::cout << "\nGDSPipeline SUMMARY\n"
              << "  events              " << n << "\n"
              << "  pixel words         " << words << "\n"
              << "  valid digis         " << valid << "  ("
              << (words ? 100.0 * double(valid) / double(words) : 0.0) << "%)\n"
              << "  gather (sum)        " << gatherMs << " ms   " << (gatherMs / double(n)) << " ms/event\n"
              << "  decode (sum)        " << decodeMs << " ms   " << (decodeMs / double(n)) << " ms/event\n"
              << "  gather+decode (sum) " << (gatherMs + decodeMs) << " ms\n"
              << "  event-loop wall     " << wallS << " s   -> " << (double(n) / wallS) << " events/s\n"
              << "  (add the source SUMMARY above for read + scan; sums overlap across EDM streams)"
              << std::endl;
  }

  static void fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
    edm::ParameterSetDescription desc;
    desc.add<edm::InputTag>("deviceSource", edm::InputTag("rawDataCollector"));
    desc.add<uint32_t>("minFedId", 1200);
    desc.add<uint32_t>("maxFedId", 1349);
    desc.add<uint32_t>("dumpDigis", 0);   // per-event digi printout
    desc.add<uint32_t>("printEvery", 0);  // 0 = per-event lines off, N = every Nth event
    descriptions.addWithDefaultLabel(desc);
  }

  void analyze(edm::StreamID, edm::Event const& event, edm::EventSetup const&) const override {
    const uint64_t tEntry = nowNs();
    // first event wins; last event keeps the max
    uint64_t expected = 0;
    firstNs_.compare_exchange_strong(expected, tEntry);

    const gdsraw::RawDataDeviceRef& ref = event.get(deviceToken_);

    const uint64_t t0 = nowNs();
    auto g = gdsgather::gatherFeds(ref, minFedId_, maxFedId_);
    const uint64_t t1 = nowNs();
    auto d = gdsdecode::decodeWords(g.d_words, g.d_fedIndex, g.nWords, minFedId_);
    const uint64_t t2 = nowNs();

    gatherNs_ += (t1 - t0);
    decodeNs_ += (t2 - t1);
    totalWords_ += g.nWords;
    totalValid_ += d.nValid;
    const uint64_t seq = nEvents_.fetch_add(1) + 1;

    uint64_t prev = lastNs_.load();
    while (t2 > prev && not lastNs_.compare_exchange_weak(prev, t2)) {
    }

    if (printEvery_ > 0 && (seq % printEvery_) == 0) {
      const double pct = g.nWords ? (100.0 * d.nValid / g.nWords) : 0.0;
      std::cout << "GDSPipeline: event " << event.id().event() << "  fragments " << ref.nFeds() << "  pixel "
                << g.nSelected << "  words " << g.nWords << "  valid " << d.nValid << " (" << pct << "%)"
                << "  gather " << ((t1 - t0) / 1e6) << " ms  decode " << ((t2 - t1) / 1e6) << " ms" << std::endl;
    }

    if (dumpDigis_ > 0 && d.nWords > 0) {
      const uint32_t nd = std::min(dumpDigis_, d.nWords);
      std::vector<gdsdecode::Digi> h(nd);
      cudaCheck(cudaMemcpy(h.data(), d.d_digis, size_t(nd) * sizeof(gdsdecode::Digi), cudaMemcpyDeviceToHost));
      for (uint32_t i = 0; i < nd; ++i)
        std::cout << "    digi " << i << ": fed " << h[i].fedId << "  link " << h[i].link << "  roc " << h[i].roc
                  << "  row " << h[i].rocRow << "  col " << h[i].rocCol << "  adc " << h[i].adc << std::endl;
    }

    cudaFree(d.d_digis);
    cudaFree(g.d_words);
    cudaFree(g.d_fedIndex);
  }

private:
  const edm::EDGetTokenT<gdsraw::RawDataDeviceRef> deviceToken_;
  const uint32_t minFedId_, maxFedId_, dumpDigis_, printEvery_;

  mutable std::atomic<uint64_t> nEvents_{0};
  mutable std::atomic<uint64_t> totalWords_{0};
  mutable std::atomic<uint64_t> totalValid_{0};
  mutable std::atomic<uint64_t> gatherNs_{0};
  mutable std::atomic<uint64_t> decodeNs_{0};
  mutable std::atomic<uint64_t> firstNs_{0};
  mutable std::atomic<uint64_t> lastNs_{0};
};

#include "FWCore/Framework/interface/MakerMacros.h"
DEFINE_FWK_MODULE(GDSPixelGatherAnalyzer);
