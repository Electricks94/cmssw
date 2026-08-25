#include <atomic>
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
#include "HeterogeneousCore/CUDAUtilities/interface/cudaCheck.h"

using namespace cms::cuda;
#define cudaCheck(ARG) cms::cuda::cudaCheck(__FILE__, __LINE__, __func__, (ARG))

// Pipeline-agnostic GPU memory monitor.
//
// Add it to any path in any config: it consumes nothing, so it runs alongside
// whatever else is scheduled. Samples cudaMemGetInfo on every visible device and
// reports the peak "used" (total - free) per device.
//
// Because it touches no products, the SAME module measures the production
// pipeline and the GDS pipeline, which is what makes the two numbers comparable.
//
// NOTE: "used" is device-wide, so it includes the CUDA context (~300-500 MB per
// device) and anything else running on the card. Take a baseline (see the config
// comment) and subtract, or make sure you have the GPUs to yourself.

class GPUMemMonitor : public edm::global::EDAnalyzer<> {
public:
  explicit GPUMemMonitor(edm::ParameterSet const& config)
      : label_(config.getParameter<std::string>("label")),
        sampleEvery_(config.getParameter<uint32_t>("sampleEvery")) {
    int n = 0;
    cudaCheck(cudaGetDeviceCount(&n));
    nDevices_ = n;
    peakUsed_ = std::vector<std::atomic<uint64_t>>(n);
    total_.resize(n, 0);
    for (int d = 0; d < n; ++d) {
      peakUsed_[d].store(0);
      sample(d);  // baseline right after construction
    }
  }

  ~GPUMemMonitor() override {
    std::cout << "\nGPUMemMonitor [" << label_ << "] peak device memory used\n";
    uint64_t sum = 0;
    for (int d = 0; d < nDevices_; ++d) {
      const uint64_t peak = peakUsed_[d].load();
      sum += peak;
      std::cout << "  device " << d << "  " << (peak / (1 << 20)) << " MiB used of "
                << (total_[d] / (1 << 20)) << " MiB\n";
    }
    std::cout << "  all devices          " << (sum / (1 << 20)) << " MiB" << std::endl;
  }

  static void fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
    edm::ParameterSetDescription desc;
    desc.add<std::string>("label", "job");
    desc.add<uint32_t>("sampleEvery", 100);  // cudaMemGetInfo costs ~20 us, do not do it every event
    descriptions.addWithDefaultLabel(desc);
  }

  void analyze(edm::StreamID, edm::Event const&, edm::EventSetup const&) const override {
    const uint64_t n = nSeen_.fetch_add(1) + 1;
    if (sampleEvery_ == 0 || (n % sampleEvery_) != 0)
      return;
    for (int d = 0; d < nDevices_; ++d)
      sample(d);
  }

private:
  void sample(int device) const {
    int prev = 0;
    if (cudaGetDevice(&prev) != cudaSuccess)
      return;
    if (cudaSetDevice(device) != cudaSuccess)
      return;
    size_t freeB = 0, totalB = 0;
    if (cudaMemGetInfo(&freeB, &totalB) == cudaSuccess) {
      total_[device] = totalB;
      const uint64_t used = uint64_t(totalB) - uint64_t(freeB);
      uint64_t prevPeak = peakUsed_[device].load();
      while (used > prevPeak && not peakUsed_[device].compare_exchange_weak(prevPeak, used)) {
      }
    }
    cudaSetDevice(prev);  // restore: the current device is per host thread
  }

  const std::string label_;
  const uint32_t sampleEvery_;
  int nDevices_ = 0;
  mutable std::vector<std::atomic<uint64_t>> peakUsed_;
  mutable std::vector<size_t> total_;
  mutable std::atomic<uint64_t> nSeen_{0};
};

#include "FWCore/Framework/interface/MakerMacros.h"
DEFINE_FWK_MODULE(GPUMemMonitor);
