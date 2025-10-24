#include "HeterogeneousCore/AlpakaInterface/interface/workdivision.h"

#include "TestKernel.h"

template<typename ConstSoAView> 
struct GetScalar {
  ALPAKA_FN_ACC auto operator()(ConstSoAView& view) const {
      return view.x2();
  }
};

namespace ALPAKA_ACCELERATOR_NAMESPACE {

  struct ConsumerKernel {
  template <typename TAcc, typename MultiView>
  ALPAKA_FN_ACC void operator()(TAcc const& acc, MultiView multiView, float* result) const {
      // loop over all elements of the manager and read the local value
    GetScalar<ConstSoAView> getX2;
    auto sum = multiView.getScalar(getX2, [](auto a, auto b) { return a + b; });
    auto firstElement = multiView.getScalar(getX2);
    for (auto local_idx : cms::alpakatools::uniform_elements(acc, multiView.size())) {
      // auto localView = multiView.getView(local_idx);
      // const float scalar = static_cast<float>(localView.x2());

      // auto slvi = multiView[local_idx];
      result[local_idx] = static_cast<float>(firstElement + sum); // (static_cast<float>(local_idx) + sum) * slvi.x1().dot(slvi.x1());
    }
    }
  };

  void TestKernel::run(Queue& queue, MultiView<SoA, 3> mv, float* result){
    uint32_t items = 64;

    // The total number of threads is determined by the size of the multiView,
    // which is the sum of the sizes of all views it holds
    uint32_t groups = cms::alpakatools::divide_up_by(mv.size(), items);

    auto grid = cms::alpakatools::make_workdiv<Acc1D>(groups, items);
    alpaka::exec<Acc1D>(queue, grid, ConsumerKernel{}, mv, result);

  }

}  // namespace ALPAKA_ACCELERATOR_NAMESPACE
