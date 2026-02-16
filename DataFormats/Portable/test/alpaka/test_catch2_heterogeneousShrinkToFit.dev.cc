#include <Eigen/Core>
#include <Eigen/Dense>

#include <alpaka/alpaka.hpp>

#define CATCH_CONFIG_MAIN
#include <catch2/catch_all.hpp>

#include "DataFormats/SoATemplate/interface/SoALayout.h"
#include "DataFormats/Portable/interface/PortableCollection.h"
#include "HeterogeneousCore/AlpakaInterface/interface/config.h"
#include "HeterogeneousCore/AlpakaInterface/interface/memory.h"
#include "HeterogeneousCore/AlpakaInterface/interface/workdivision.h"

using namespace Catch::Matchers;
using namespace ALPAKA_ACCELERATOR_NAMESPACE;

using Matrix6x4d = Eigen::Matrix<double, 6, 4>;

GENERATE_SOA_LAYOUT(SoATemplate,
                    SOA_COLUMN(float, x),
                    SOA_COLUMN(float, y),
                    SOA_COLUMN(float, z),
                    SOA_EIGEN_COLUMN(Eigen::Vector3d, candidateDirection),
                    SOA_EIGEN_COLUMN(Matrix6x4d, matrix),
                    SOA_SCALAR(int, detectorType))

using SoA = SoATemplate<>;
using SoAView = SoA::View;
using SoAConstView = SoA::ConstView;

struct FillSoA {
  template <alpaka::concepts::Acc TAcc>
  ALPAKA_FN_ACC void operator()(TAcc const& acc, SoAView view) const {
    const auto elems = view.metadata().size();
    if (cms::alpakatools::once_per_grid(acc))
      view.detectorType() = 6;

    for (auto local_idx : cms::alpakatools::uniform_elements(acc, elems)) {
      view[local_idx].x() = 0.0f * static_cast<float>(elems) + static_cast<float>(local_idx);
      view[local_idx].y() = 1.0f * static_cast<float>(elems) + static_cast<float>(local_idx);
      view[local_idx].z() = 2.0f * static_cast<float>(elems) + static_cast<float>(local_idx);

      view[local_idx].candidateDirection().setConstant(4.0f);
      view[local_idx].matrix().setConstant(5.0f);
    }
  }
};

void requireContentEqual(auto& queue, auto& baseCollection, auto& shrinkedCollection) {
  PortableHostCollection<SoA> baseHostCollection(queue, baseCollection.size());
  PortableHostCollection<SoA> shrinkedHostCollection(queue, shrinkedCollection.size());

  alpaka::memcpy(queue, baseHostCollection.buffer(), baseCollection.buffer());
  alpaka::memcpy(queue, shrinkedHostCollection.buffer(), shrinkedCollection.buffer());

  alpaka::wait(queue);

  const SoAConstView& baseView = baseHostCollection.const_view();
  const SoAConstView& shrinkedView = shrinkedHostCollection.const_view();

  REQUIRE(baseView.detectorType() == shrinkedView.detectorType());
  for (auto i = 0; i < shrinkedView.metadata().size(); ++i) {
    REQUIRE_THAT(baseView[i].x(), WithinRel(shrinkedView[i].x()));
    REQUIRE_THAT(baseView[i].y(), WithinRel(shrinkedView[i].y()));
    REQUIRE_THAT(baseView[i].z(), WithinRel(shrinkedView[i].z()));

    REQUIRE(baseView[i].candidateDirection().isApprox(shrinkedView[i].candidateDirection()));
    REQUIRE(baseView[i].matrix().isApprox(shrinkedView[i].matrix()));
  }
}

TEST_CASE("test shrinkToFit soa alpaka", "[ShrinkToFit][Alpaka]") {
  auto const& devices = cms::alpakatools::devices<Platform>();
  if (devices.empty()) {
    std::cout << "No devices available for the " << EDM_STRINGIZE(ALPAKA_ACCELERATOR_NAMESPACE)
              << " backend, skipping.\n";
    return;
  }

  for (auto const& device : devices) {
    std::cout << "Running on " << alpaka::getName(device) << std::endl;
    Queue queue(device);

    constexpr int n1 = 343;
    constexpr int n2 = 117;

    PortableCollection<Device, SoA> baseCollection(queue, n1);
    SoAView baseView = baseCollection.view();

     // fill up the collection with some data
    auto blockSize = 64;
    auto numberOfBlocks = cms::alpakatools::divide_up_by(n1, blockSize);

    const auto workDiv = cms::alpakatools::make_workdiv<Acc1D>(numberOfBlocks, blockSize);

    alpaka::exec<Acc1D>(queue, workDiv, FillSoA{}, baseView);
    alpaka::wait(queue);

    SECTION("Shrink to fit host to host and device to device") {
      // Smaller PortableCollection, that will host the beginning of the data of the base collection
      PortableCollection<Device, SoA> shrinkedCollection(queue, n2);
      shrinkedCollection.deepCopy(queue, baseView);

      // Check the results
      requireContentEqual(queue, baseCollection, shrinkedCollection);
    }

    SECTION("Shrink to fit device to host") {
      PortableHostCollection<SoA> shrinkedHostCollection(queue, n2);
      shrinkedHostCollection.deepCopy(queue, baseView);

      // Check the results
      requireContentEqual(queue, baseCollection, shrinkedHostCollection);
    }

    SECTION("Shrink to fit host to device") {
      PortableHostCollection<SoA> baseHostCollection(queue, n1);
      alpaka::memcpy(queue, baseHostCollection.buffer(), baseCollection.buffer());

      PortableCollection<Device, SoA> shrinkedDeviceCollection(queue, n2);
      shrinkedDeviceCollection.deepCopy(queue, baseHostCollection.const_view());

      // Check the results
      requireContentEqual(queue, baseHostCollection, shrinkedDeviceCollection);
    }
  }
}
