#include <Eigen/Core>
#include <Eigen/Dense>

#include <alpaka/alpaka.hpp>

#define CATCH_CONFIG_MAIN
#include <catch.hpp>

#include "DataFormats/Portable/interface/alpaka/MultiSoAViewManager.h"
#include "DataFormats/Portable/interface/PortableHostCollection.h"
#include "DataFormats/Portable/interface/PortableCollection.h"
#include "DataFormats/SoATemplate/interface/SoACommon.h"
#include "DataFormats/SoATemplate/interface/SoALayout.h"

#include "HeterogeneousCore/AlpakaInterface/interface/config.h"
#include "HeterogeneousCore/AlpakaInterface/interface/memory.h"
#include "HeterogeneousCore/AlpakaInterface/interface/workdivision.h"

using namespace ALPAKA_ACCELERATOR_NAMESPACE;

GENERATE_SOA_LAYOUT(SoATemplate, 
                    SOA_COLUMN(float, x0),
                    SOA_EIGEN_COLUMN(Eigen::Vector3d, x1),
                    SOA_SCALAR(int, x2))

using SoA = SoATemplate<>;
using SoAView = SoA::View;

struct TestKernel {
  template <typename TAcc, typename SoAManager>
  ALPAKA_FN_ACC void operator()(TAcc const& acc, SoAManager manager) const {
    for (auto local_idx : cms::alpakatools::uniform_elements(acc, manager.size())) {
      auto localView = manager.getView(local_idx);
      const float scalar = static_cast<float>(localView.x2());

      auto slvi = manager[local_idx];
      slvi.x0() = (static_cast<float>(local_idx) + scalar) * slvi.x1().dot(slvi.x1());
    }
  }
};

TEST_CASE("Test Multi View Manager") {
  auto const& devices = cms::alpakatools::devices<Platform>();
  if (devices.empty()) {
    FAIL("No devices available for the " EDM_STRINGIZE(ALPAKA_ACCELERATOR_NAMESPACE) " backend, "
        "the test will be skipped.");
  }

  for (auto const& device : cms::alpakatools::devices<Platform>()) {
    std::cout << "Running on " << alpaka::getName(device) << std::endl;

    const cms::soa::size_type elements1 = 5;
    const cms::soa::size_type elements2 = 25;
    const cms::soa::size_type elements3 = 13;
    const cms::soa::size_type elements4 = 41;

    PortableHostCollection<SoA> collection1(elements1, cms::alpakatools::host());
    PortableHostCollection<SoA> collection2(elements2, cms::alpakatools::host());
    PortableHostCollection<SoA> collection3(elements3, cms::alpakatools::host());
    PortableHostCollection<SoA> collection4(elements4, cms::alpakatools::host());

    SoAView soav1 = collection1.view();
    SoAView soav2 = collection2.view();
    SoAView soav3 = collection3.view();
    SoAView soav4 = collection4.view();

    soav1.x2() = static_cast<int>(elements1);
    soav2.x2() = static_cast<int>(elements2);
    soav3.x2() = static_cast<int>(elements3);
    soav4.x2() = static_cast<int>(elements4);

    for (SoAView::size_type i = 0; i < soav1.metadata().size(); ++i) {
      soav1[i] = {1.0f, {2.0f, 3.0f, 4.0f}};
    }

    for (SoAView::size_type i = 0; i < soav2.metadata().size(); ++i) {
      soav2[i] = {5.0f, {6.0f, 7.0f, 8.0f}};
    }

    for (SoAView::size_type i = 0; i < soav3.metadata().size(); ++i) {
      soav3[i] = {9.0f, {10.0f, 11.0f, 12.0f}};
    }

    for (SoAView::size_type i = 0; i < soav4.metadata().size(); ++i) {
      soav4[i] = {13.0f, {14.0f, 15.0f, 16.0f}};
    }

    SECTION("Host Multi View Manager") {
      auto manager = MultiSoAViewManager(soav1, soav2, soav3, soav4);
      REQUIRE(manager.size() == elements1 + elements2 + elements3 + elements4);

      // Loop over all elements of the four views
      for (std::size_t local_idx = 0; local_idx < manager.size(); ++local_idx) {
        auto localView = manager.getView(local_idx);
        const float scalar = static_cast<float>(localView.x2());
        auto slvi = manager[local_idx];
        slvi.x0() = scalar * slvi.x0() * slvi.x1().dot(slvi.x1());
      }

      for (SoAView::size_type i = 0; i < soav1.metadata().size(); ++i) {
        const int result = static_cast<int>(soav1[i].x0());
        REQUIRE(result == 145);
      }

      for (SoAView::size_type i = 0; i < soav2.metadata().size(); ++i) {
        const int result = static_cast<int>(soav2[i].x0());
        REQUIRE(result == 18625);
      }

      for (SoAView::size_type i = 0; i < soav3.metadata().size(); ++i) {
        const int result = static_cast<int>(soav3[i].x0());
        REQUIRE(result == 42705);
      }

      for (SoAView::size_type i = 0; i < soav4.metadata().size(); ++i) {
        const int result = static_cast<int>(soav4[i].x0());
        REQUIRE(result == 360841);
      }
    }

    SECTION("Device Multi View Manager") {
      const cms::soa::size_type elements5 = 7;
      const cms::soa::size_type elements6 = 22;

      Queue queue(device);

      PortableHostCollection<SoA> hostCollection1(elements5, queue);
      PortableHostCollection<SoA> hostCollection2(elements6, queue);

      PortableCollection<SoA, Device> deviceCollection1(elements5, queue);
      PortableCollection<SoA, Device> deviceCollection2(elements6, queue);

      SoAView h_view1 = hostCollection1.view();
      SoAView h_view2 = hostCollection2.view();

      h_view1.x2() = elements5;
      h_view2.x2() = elements6;

      for (SoAView::size_type i = 0; i < h_view1.metadata().size(); ++i) {
        h_view1[i] = {1.0f, {2.0f, 3.0f, 4.0f}};
      }

      for (SoAView::size_type i = 0; i < h_view2.metadata().size(); ++i) {
        h_view2[i] = {5.0f, {6.0f, 7.0f, 8.0f}};
      }

      alpaka::memcpy(queue, deviceCollection1.buffer(), hostCollection1.buffer());
      alpaka::memcpy(queue, deviceCollection2.buffer(), hostCollection2.buffer());

      alpaka::wait(queue);

      auto deviceManager = MultiSoAViewManager(deviceCollection1.view(), deviceCollection2.view());
      REQUIRE(deviceManager.size() == elements5 + elements6);

      auto blockSize = 64;
      auto numberOfBlocks = cms::alpakatools::divide_up_by(deviceManager.size(), blockSize);
      const auto workDiv = cms::alpakatools::make_workdiv<Acc1D>(numberOfBlocks, blockSize);

      alpaka::exec<Acc1D>(queue, workDiv, TestKernel{}, deviceManager);

      alpaka::wait(queue);

      alpaka::memcpy(queue, hostCollection1.buffer(), deviceCollection1.buffer());
      alpaka::memcpy(queue, hostCollection2.buffer(), deviceCollection2.buffer());

      alpaka::wait(queue);

      auto checkManager = MultiSoAViewManager(hostCollection1.view(), hostCollection2.view());
      REQUIRE(deviceManager.size() == checkManager.size());

      const int d1 = 2.0 * 2.0 + 3.0 * 3.0 + 4.0 * 4.0;
      const int d2 = 6.0 * 6.0 + 7.0 * 7.0 + 8.0 * 8.0;

      for (std::size_t i = 0; i < checkManager.size(); ++i) {
        auto slvi = checkManager[i];
        const int result = static_cast<int>(slvi.x0());
        const int check = i < elements5 ? (i + elements5) * d1 : (i + elements6) * d2;

        REQUIRE(result == check);
      }
    }
  }
}
