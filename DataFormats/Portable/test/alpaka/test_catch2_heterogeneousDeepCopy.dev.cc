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

GENERATE_SOA_LAYOUT(SoAPositionTemplate,
                    SOA_COLUMN(float, x),
                    SOA_COLUMN(float, y),
                    SOA_COLUMN(float, z),
                    SOA_SCALAR(int, detectorType))

using SoAPosition = SoAPositionTemplate<>;
using SoAPositionView = SoAPosition::View;
using SoAPositionConstView = SoAPosition::ConstView;

GENERATE_SOA_LAYOUT(SoAPCATemplate,
                    SOA_COLUMN(float, eigenvalues),
                    SOA_COLUMN(float, eigenvector_1),
                    SOA_COLUMN(float, eigenvector_2),
                    SOA_COLUMN(float, eigenvector_3),
                    SOA_EIGEN_COLUMN(Eigen::Vector3d, candidateDirection))

using SoAPCA = SoAPCATemplate<>;
using SoAPCAView = SoAPCA::View;
using SoAPCAConstView = SoAPCA::ConstView;

GENERATE_SOA_LAYOUT(GenericSoATemplate,
                    SOA_SCALAR(int, detType),
                    SOA_COLUMN(float, x),
                    SOA_COLUMN(float, y),
                    SOA_COLUMN(float, z),
                    SOA_EIGEN_COLUMN(Eigen::Vector3d, candidateDirection))

using GenericSoA = GenericSoATemplate<cms::soa::CacheLineSize::IntelCPU>;
using GenericSoAView = GenericSoA::View;
using GenericSoAConstView = GenericSoA::ConstView;

// Kernel for filling the SoA
struct FillSoA {
  template <alpaka::concepts::Acc TAcc, typename PositionView, typename PCAView>
  ALPAKA_FN_ACC void operator()(TAcc const& acc, PositionView positionView, PCAView pcaView) const {
    constexpr float interval = 0.01f;
    const auto elems = positionView.metadata().size();
    if (cms::alpakatools::once_per_grid(acc))
      positionView.detectorType() = 1;

    for (auto local_idx : cms::alpakatools::uniform_elements(acc, elems)) {
      positionView[local_idx].x() = 0.0f * static_cast<float>(elems) + static_cast<float>(local_idx);
      positionView[local_idx].y() = 1.0f * static_cast<float>(elems) + static_cast<float>(local_idx);
      positionView[local_idx].z() = 2.0f * static_cast<float>(elems) + static_cast<float>(local_idx);

      pcaView[local_idx].eigenvector_1() = positionView[local_idx].x() / interval;
      pcaView[local_idx].eigenvector_2() = positionView[local_idx].y() / interval;
      pcaView[local_idx].eigenvector_3() = positionView[local_idx].z() / interval;
      pcaView[local_idx].candidateDirection()(0) = positionView[local_idx].x() / interval;
      pcaView[local_idx].candidateDirection()(1) = positionView[local_idx].y() / interval;
      pcaView[local_idx].candidateDirection()(2) = positionView[local_idx].z() / interval;
    }
  }
};

void requireSameAddresses(auto genericView, auto positionView, auto pcaView) {
  REQUIRE(genericView.metadata().addressOf_detType() == positionView.metadata().addressOf_detectorType());
  REQUIRE(genericView.metadata().addressOf_x() == positionView.metadata().addressOf_x());
  REQUIRE(genericView.metadata().addressOf_y() == positionView.metadata().addressOf_y());
  REQUIRE(genericView.metadata().addressOf_z() == positionView.metadata().addressOf_z());
  REQUIRE(genericView.metadata().addressOf_candidateDirection() == pcaView.metadata().addressOf_candidateDirection());
}

void requireDifferentAddresses(auto genericView, auto positionView, auto pcaView) {
  REQUIRE(genericView.metadata().addressOf_detType() != positionView.metadata().addressOf_detectorType());
  REQUIRE(genericView.metadata().addressOf_x() != positionView.metadata().addressOf_x());
  REQUIRE(genericView.metadata().addressOf_y() != positionView.metadata().addressOf_y());
  REQUIRE(genericView.metadata().addressOf_z() != positionView.metadata().addressOf_z());
  REQUIRE(genericView.metadata().addressOf_candidateDirection() != pcaView.metadata().addressOf_candidateDirection());
}

void requireContentEqual(auto& queue, auto& genericCollection, auto& positionCollection, auto& pcaCollection) {
  PortableHostCollection<GenericSoA> genericHostCollection(queue, genericCollection.size());
  PortableHostCollection<SoAPosition> positionHostCollection(queue, positionCollection.size());
  PortableHostCollection<SoAPCA> pcaHostCollection(queue, pcaCollection.size());

  alpaka::memcpy(queue, genericHostCollection.buffer(), genericCollection.buffer());
  alpaka::memcpy(queue, positionHostCollection.buffer(), positionCollection.buffer());
  alpaka::memcpy(queue, pcaHostCollection.buffer(), pcaCollection.buffer());

  alpaka::wait(queue);

  const GenericSoAConstView& genericView = genericHostCollection.const_view();
  const SoAPositionConstView& positionView = positionHostCollection.const_view();
  const SoAPCAConstView& pcaView = pcaHostCollection.const_view();

  REQUIRE(genericView.detType() == positionView.detectorType());
  for (auto i = 0; i < genericView.metadata().size(); ++i) {
    REQUIRE_THAT(genericView[i].x(), WithinRel(positionView[i].x()));
    REQUIRE_THAT(genericView[i].y(), WithinRel(positionView[i].y()));
    REQUIRE_THAT(genericView[i].z(), WithinRel(positionView[i].z()));

    REQUIRE(genericView[i].candidateDirection().isApprox(pcaView[i].candidateDirection()));
  }
}

TEST_CASE("Deep copy from SoA Generic View") {
  auto const& devices = cms::alpakatools::devices<Platform>();
  if (devices.empty()) {
    FAIL("No devices available for the " EDM_STRINGIZE(ALPAKA_ACCELERATOR_NAMESPACE) " backend, "
        "the test will be skipped.");
  }

  for (auto const& device : cms::alpakatools::devices<Platform>()) {
    std::cout << "Running on " << alpaka::getName(device) << std::endl;

    Queue queue(device);

    // common number of elements for the SoAs
    const std::size_t elems = 10;

    // Portable Collections
    PortableCollection<Device, SoAPosition> positionCollection(queue, elems);
    PortableCollection<Device, SoAPCA> pcaCollection(queue, elems);

    // Portable Collection Views
    SoAPositionView& positionCollectionView = positionCollection.view();
    SoAPCAView& pcaCollectionView = pcaCollection.view();
    // Portable Collection ConstViews
    const SoAPositionConstView& positionCollectionConstView = positionCollection.const_view();
    const SoAPCAConstView& pcaCollectionConstView = pcaCollection.const_view();

    // fill up
    auto blockSize = 64;
    auto numberOfBlocks = cms::alpakatools::divide_up_by(elems, blockSize);

    const auto workDiv = cms::alpakatools::make_workdiv<Acc1D>(numberOfBlocks, blockSize);

    alpaka::exec<Acc1D>(queue, workDiv, FillSoA{}, positionCollectionView, pcaCollectionView);

    alpaka::wait(queue);

    SECTION("Deep copy the View host to host and device to device") {
      // addresses and size of the SoA columns
      const auto posRecs = positionCollectionView.records();
      const auto pcaRecs = pcaCollectionView.records();

      // building the View with runtime check for the size
      GenericSoAView genericView(
          posRecs.detectorType(), posRecs.x(), posRecs.y(), posRecs.z(), pcaRecs.candidateDirection());

      // Check for equality of memory addresses
      requireSameAddresses(genericView, positionCollectionView, pcaCollectionView);

      // PortableCollection that will host the aggregated columns
      PortableCollection<Device, GenericSoA> genericCollection(queue, elems);
      genericCollection.deepCopy(queue, genericView);

      // Check for inequality of memory addresses
      requireContentEqual(queue, genericCollection, positionCollection, pcaCollection);
    }

    SECTION("Deep copy the ConstView host to host and device to device") {
      // addresses and size of the SoA columns
      const auto posRecs = positionCollectionConstView.records();
      const auto pcaRecs = pcaCollectionConstView.records();

      // building the View with runtime check for the size
      GenericSoAConstView genericConstView(
          posRecs.detectorType(), posRecs.x(), posRecs.y(), posRecs.z(), pcaRecs.candidateDirection());

      // Check for equality of memory addresses
      requireSameAddresses(genericConstView, positionCollectionView, pcaCollectionView);

      // PortableCollection that will host the aggregated columns
      PortableCollection<Device, GenericSoA> genericCollection(queue, elems);
      genericCollection.deepCopy(queue, genericConstView);

      // Check for inequality of memory addresses
      requireDifferentAddresses(genericCollection.view(), positionCollectionView, pcaCollectionView);
      requireContentEqual(queue, genericCollection, positionCollection, pcaCollection);
    }

    SECTION("Deep copy the ConstView device to host") {
      // addresses and size of the SoA columns
      const auto posRecs = positionCollectionConstView.records();
      const auto pcaRecs = pcaCollectionConstView.records();

      // building the View with runtime check for the size
      GenericSoAConstView genericConstView(
          posRecs.detectorType(), posRecs.x(), posRecs.y(), posRecs.z(), pcaRecs.candidateDirection());

      // Check for equality of memory addresses
      requireSameAddresses(genericConstView, positionCollectionView, pcaCollectionView);

      // PortableCollection that will host the aggregated columns
      PortableHostCollection<GenericSoA> genericCollection(queue, elems);
      genericCollection.deepCopy(queue, genericConstView);

      // Check for inequality of memory addresses
      requireDifferentAddresses(genericCollection.view(), positionCollectionView, pcaCollectionView);
      requireContentEqual(queue, genericCollection, positionCollection, pcaCollection);
    }

    SECTION("Deep copy the ConstView host to device") {
      PortableHostCollection<SoAPosition> positionHostCollection(queue, elems);
      PortableHostCollection<SoAPCA> pcaHostCollection(queue, elems);

      alpaka::memcpy(queue, positionHostCollection.buffer(), positionCollection.buffer());
      alpaka::memcpy(queue, pcaHostCollection.buffer(), pcaCollection.buffer());

      const SoAPositionConstView& positionViewHostCollection = positionHostCollection.const_view();
      const SoAPCAConstView& pcaViewHostCollection = pcaHostCollection.const_view();

      // addresses and size of the SoA columns
      const auto posRecs = positionViewHostCollection.records();
      const auto pcaRecs = pcaViewHostCollection.records();

      // building the View with runtime check for the size
      GenericSoAConstView genericConstView(
          posRecs.detectorType(), posRecs.x(), posRecs.y(), posRecs.z(), pcaRecs.candidateDirection());

      // Check for equality of memory addresses
      requireSameAddresses(genericConstView, positionViewHostCollection, pcaViewHostCollection);

      // PortableCollection that will host the aggregated columns
      PortableCollection<Device, GenericSoA> genericCollection(queue, elems);
      genericCollection.deepCopy(queue, genericConstView);

      // Check for inequality of memory addresses
      requireDifferentAddresses(genericCollection.view(), positionViewHostCollection, pcaViewHostCollection);
      requireContentEqual(queue, genericCollection, positionHostCollection, pcaHostCollection);
    }
  }
}
