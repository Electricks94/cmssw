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

using namespace ALPAKA_ACCELERATOR_NAMESPACE;

GENERATE_SOA_LAYOUT(SoATemplate,
                    SOA_COLUMN(float, eigenvalues),
                    SOA_COLUMN(float, eigenvector_1),
                    SOA_COLUMN(float, eigenvector_2),
                    SOA_COLUMN(float, eigenvector_3),
                    SOA_EIGEN_COLUMN(Eigen::Vector3d, candidateDirection),
                    SOA_SCALAR(int, detectorType))

using SoA = SoATemplate<>;
using SoAView = SoA::View;
using SoAConstView = SoA::ConstView;

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

    constexpr int n1 = 10;
    constexpr int n2 = n1 / 2;

    PortableHostCollection<SoA> hostCollection(cms::alpakatools::host(), n1);
    auto h_view1 = hostCollection.view();

    // fill up
    for (int i = 0; i < hostCollection.size(); i++) {
      h_view1[i].eigenvalues() = static_cast<float>(1);
      h_view1[i].eigenvector_1() = static_cast<float>(2);
      h_view1[i].eigenvector_2() = static_cast<float>(3);
      h_view1[i].eigenvector_3() = static_cast<float>(4);
      h_view1[i].candidateDirection().setConstant(5.f);
    }
    h_view1.detectorType() = 6;

    PortableCollection<Device, SoA> deviceCollection(queue, hostCollection.size());
    auto d_view1 = deviceCollection.view();
    alpaka::memcpy(queue, deviceCollection.buffer(), hostCollection.buffer());

    alpaka::wait(queue);

    // shrink to fit to a smaller size
    PortableCollection<Device, SoA> shrinkedDeviceCollection(queue, n2);
    shrinkedDeviceCollection.deepCopy(queue, d_view1);

    PortableHostCollection<SoA> shrinkedHostCollection(cms::alpakatools::host(), shrinkedDeviceCollection.size());
    auto h_viewShrinked = shrinkedHostCollection.view();
    alpaka::memcpy(queue, shrinkedHostCollection.buffer(), shrinkedDeviceCollection.buffer());

    alpaka::wait(queue);

    REQUIRE(shrinkedHostCollection.size() == n2);
    REQUIRE(h_viewShrinked.detectorType() == 6);

    for (int i = 0; i < n2; i++) {
        REQUIRE(h_viewShrinked[i].eigenvalues() == Catch::Approx(1.0f));
        REQUIRE(h_viewShrinked[i].eigenvector_1() == Catch::Approx(2.0f));
        REQUIRE(h_viewShrinked[i].eigenvector_2() == Catch::Approx(3.0f));
        REQUIRE(h_viewShrinked[i].eigenvector_3() == Catch::Approx(4.0f));
        REQUIRE(h_viewShrinked[i].candidateDirection().x() == Catch::Approx(5.0f));
        REQUIRE(h_viewShrinked[i].candidateDirection().y() == Catch::Approx(5.0f));
        REQUIRE(h_viewShrinked[i].candidateDirection().z() == Catch::Approx(5.0f));
    }
  }
}
