#include "HeterogeneousCore/AlpakaInterface/interface/workdivision.h"

#include "TestKernel.h"

namespace ALPAKA_ACCELERATOR_NAMESPACE {

  struct ConsumerKernel {
  template <typename TAcc, typename SoAManager>
  ALPAKA_FN_ACC void operator()(TAcc const& acc, SoAManager manager, float* result) const {
      // loop over all elements of the manager and read the local value
    for (auto local_idx : cms::alpakatools::uniform_elements(acc, manager.size())) {
      auto localView = manager.getView(local_idx);
      const float scalar = static_cast<float>(localView.x2());

      auto slvi = manager[local_idx];
      result[local_idx] = (static_cast<float>(local_idx) + scalar) * slvi.x1().dot(slvi.x1());
    }
    }
  };

  void TestKernel::run(Queue& queue, DeviceCollectionManager& manager, float* result){
    auto view = manager.makeFlatView();  // by value
    uint32_t items = 64;

    // The total number of threads is determined by the size of the manager,
    // which is the sum of the sizes of all views it holds
    uint32_t groups = cms::alpakatools::divide_up_by(view.size(), items);

    auto grid = cms::alpakatools::make_workdiv<Acc1D>(groups, items);
    alpaka::exec<Acc1D>(queue, grid, ConsumerKernel{}, view, result);

  }

}  // namespace ALPAKA_ACCELERATOR_NAMESPACE
