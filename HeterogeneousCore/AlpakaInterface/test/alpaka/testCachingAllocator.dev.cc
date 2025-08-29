#include <alpaka/alpaka.hpp>
#include <cstddef>

#define CATCH_CONFIG_MAIN
#include <catch.hpp>

#include "HeterogeneousCore/AlpakaInterface/interface/AllocatorConfig.h"
#include "HeterogeneousCore/AlpakaInterface/interface/CachingAllocator.h"
#include "HeterogeneousCore/AlpakaInterface/interface/config.h"
#include "HeterogeneousCore/AlpakaInterface/interface/memory.h"
#include "HeterogeneousCore/AlpakaInterface/interface/workdivision.h"



using namespace ALPAKA_ACCELERATOR_NAMESPACE;

struct TestKernel {
  template <typename TAcc>
  ALPAKA_FN_ACC void operator()(TAcc const& acc, float* testData, const std::size_t N) const {
    for (auto local_idx : cms::alpakatools::uniform_elements(acc, N)) {
        testData[local_idx] = static_cast<float>(local_idx);
    }
  }
};


TEST_CASE("Test CachingAllocator") {
  auto const& devices = cms::alpakatools::devices<Platform>();
  if (devices.empty()) {
    FAIL("No devices available for the " EDM_STRINGIZE(ALPAKA_ACCELERATOR_NAMESPACE) " backend, "
         "the test will be skipped.");
  }

  for (auto const& device : cms::alpakatools::devices<Platform>()) {
    std::cout << "Running on " << alpaka::getName(device) << std::endl;
    Queue queue(device);

    const bool debug = true;
    auto config      = cms::alpakatools::AllocatorConfig{};
    auto& allocator  = cms::alpakatools::getDeviceCachingAllocator<Device, Queue>(device, config, debug);

    const std::size_t nElements = 10;
    const std::size_t sizeBytes = nElements * sizeof(float);
    std::cout << "sizeBytes: " << sizeBytes << std::endl;

    const Idx blockSize = 64;
    const Idx numberOfBlocks = cms::alpakatools::divide_up_by(static_cast<Idx>(nElements), blockSize);
    const auto workDiv = cms::alpakatools::make_workdiv<Acc1D>(numberOfBlocks, blockSize);

    void* memPtr1 = allocator.allocate(sizeBytes, queue);
    float* testData1 = reinterpret_cast<float*>(memPtr1);
    alpaka::exec<Acc1D>(queue, workDiv, TestKernel{}, testData1, nElements);
    alpaka::wait(queue);
    allocator.free(memPtr1);
 
    void* memPtr2 = allocator.allocate(sizeBytes, queue);
    float* testData2 = reinterpret_cast<float*>(memPtr2);
    alpaka::exec<Acc1D>(queue, workDiv, TestKernel{}, testData2, nElements);
    alpaka::wait(queue);
    allocator.free(memPtr2);
    

    alpaka::wait(queue);


  }
}