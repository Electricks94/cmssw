#include <Eigen/Core>
#include <Eigen/Dense>

#include <catch2/catch_all.hpp>

#include "DataFormats/Portable/interface/PortableHostCollection.h"
#include "DataFormats/SoATemplate/interface/SoACommon.h"
#include "DataFormats/SoATemplate/interface/SoALayout.h"

using namespace Catch::Matchers;

GENERATE_SOA_LAYOUT(SoAPositionTemplate,
                    SOA_COLUMN(float, x),
                    SOA_COLUMN(float, y),
                    SOA_COLUMN(float, z),
                    SOA_SCALAR(int, detectorType))

using SoAPosition = SoAPositionTemplate<>;
using SoAPositionView = SoAPosition::View;
using SoAPositionConstView = SoAPosition::ConstView;

GENERATE_SOA_LAYOUT(SoAPCATemplate,
                    SOA_COLUMN(float, vector_1),
                    SOA_COLUMN(float, vector_2),
                    SOA_COLUMN(float, vector_3),
                    SOA_EIGEN_COLUMN(Eigen::Vector3d, candidateDirection))

using SoAPCA = SoAPCATemplate<>;
using SoAPCAView = SoAPCA::View;
using SoAPCAConstView = SoAPCA::ConstView;

GENERATE_SOA_LAYOUT(GenericSoATemplate,
                    SOA_SCALAR(int, detType),
                    SOA_COLUMN(float, xPos),
                    SOA_COLUMN(float, yPos),
                    SOA_COLUMN(float, zPos),
                    SOA_EIGEN_COLUMN(Eigen::Vector3d, candidateDirection))

using GenericSoA = GenericSoATemplate<>;
using GenericSoAView = GenericSoA::View;
using GenericSoAConstView = GenericSoA::ConstView;

void requireSameAddresses(auto genericView, auto positionView, auto pcaView) {
  REQUIRE(genericView.metadata().addressOf_detType() == positionView.metadata().addressOf_detectorType());
  REQUIRE(genericView.metadata().addressOf_xPos() == positionView.metadata().addressOf_x());
  REQUIRE(genericView.metadata().addressOf_yPos() == positionView.metadata().addressOf_y());
  REQUIRE(genericView.metadata().addressOf_zPos() == positionView.metadata().addressOf_z());
  REQUIRE(genericView.metadata().addressOf_candidateDirection() == pcaView.metadata().addressOf_candidateDirection());
}

void requireDifferentAddresses(auto genericView, auto positionView, auto pcaView) {
  REQUIRE(genericView.metadata().addressOf_detType() != positionView.metadata().addressOf_detectorType());
  REQUIRE(genericView.metadata().addressOf_xPos() != positionView.metadata().addressOf_x());
  REQUIRE(genericView.metadata().addressOf_yPos() != positionView.metadata().addressOf_y());
  REQUIRE(genericView.metadata().addressOf_zPos() != positionView.metadata().addressOf_z());
  REQUIRE(genericView.metadata().addressOf_candidateDirection() != pcaView.metadata().addressOf_candidateDirection());
}

void requireContentEqual(auto genericView, auto positionView, auto pcaView, std::size_t elems) {
  REQUIRE(genericView.detType() == positionView.detectorType());
  for (std::size_t i = 0; i < elems; ++i) {
    REQUIRE_THAT(genericView[i].xPos(), WithinRel(positionView[i].x()));
    REQUIRE_THAT(genericView[i].yPos(), WithinRel(positionView[i].y()));
    REQUIRE_THAT(genericView[i].zPos(), WithinRel(positionView[i].z()));

    REQUIRE(genericView[i].candidateDirection().isApprox(pcaView[i].candidateDirection()));
  }
}

TEST_CASE("Deep copy from SoA Generic View") {
  // common number of elements for the SoAs
  const std::size_t elems = 100;

  // Portable Collections
  PortableHostCollection<SoAPosition> positionCollection(cms::alpakatools::host(), elems);
  PortableHostCollection<SoAPCA> pcaCollection(cms::alpakatools::host(), elems);

  // Portable Collection Views
  SoAPositionView& positionCollectionView = positionCollection.view();
  SoAPCAView& pcaCollectionView = pcaCollection.view();
  // Portable Collection ConstViews
  const SoAPositionConstView& positionCollectionConstView = positionCollection.const_view();
  const SoAPCAConstView& pcaCollectionConstView = pcaCollection.const_view();

  // fill up
  for (size_t i = 0; i < elems; i++) {
    positionCollectionView[i] = {0.0f * static_cast<float>(elems) + static_cast<float>(i),
                                 1.0f * static_cast<float>(elems) + static_cast<float>(i),
                                 2.0f * static_cast<float>(elems) + static_cast<float>(i)};
  }
  positionCollectionView.detectorType() = 1;

  float time = 0.01;
  for (size_t i = 0; i < elems; i++) {
    pcaCollectionView[i].vector_1() = positionCollectionView[i].x() / time;
    pcaCollectionView[i].vector_2() = positionCollectionView[i].y() / time;
    pcaCollectionView[i].vector_3() = positionCollectionView[i].z() / time;
    pcaCollectionView[i].candidateDirection()(0) = positionCollectionView[i].x() / time;
    pcaCollectionView[i].candidateDirection()(1) = positionCollectionView[i].y() / time;
    pcaCollectionView[i].candidateDirection()(2) = positionCollectionView[i].z() / time;
  }

  // addresses and size of the SoA columns
  const auto posRecs = positionCollectionView.records();
  const auto pcaRecs = pcaCollectionView.records();

  // addresses and size of the const SoA columns
  const auto constPosRecs = positionCollectionConstView.records();
  const auto constPcaRecs = pcaCollectionConstView.records();

  // building the View with runtime check for the size
  GenericSoAView genericView(
      posRecs.detectorType(), posRecs.x(), posRecs.y(), posRecs.z(), pcaRecs.candidateDirection());

  // Check for equality of memory addresses
  requireSameAddresses(genericView, positionCollectionView, pcaCollectionView);

  // building the ConstView with runtime check for the size
  GenericSoAConstView genericConstView(constPosRecs.detectorType(),
                                       constPosRecs.x(),
                                       constPosRecs.y(),
                                       constPosRecs.z(),
                                       constPcaRecs.candidateDirection());

  // Check for equality of memory addresses
  requireSameAddresses(genericConstView, positionCollectionView, pcaCollectionView);

  SECTION("Deep copy the View") {
    SECTION("Views with same size") {
      // PortableHostCollection that will host the aggregated columns
      PortableHostCollection<GenericSoA> genericCollection(cms::alpakatools::host(), elems);
      genericCollection.deepCopy(genericView);

      GenericSoAView genericCollectionView = genericCollection.view();

      // Check for inequality of memory addresses
      requireDifferentAddresses(genericCollection.view(), positionCollectionView, pcaCollectionView);
      requireContentEqual(genericCollectionView, positionCollectionView, pcaCollectionView, elems);
    }

    SECTION("Views with different size") {
      const auto smallerSize = elems / 2;
      PortableHostCollection<GenericSoA> genericCollection(cms::alpakatools::host(), smallerSize);
      genericCollection.deepCopy(genericView);

      GenericSoAView genericCollectionView = genericCollection.view();

      // Check for inequality of memory addresses
      requireDifferentAddresses(genericCollection.view(), positionCollectionView, pcaCollectionView);

      REQUIRE(genericView.metadata().size() == elems);
      REQUIRE(genericCollectionView.metadata().size() == smallerSize);
      requireContentEqual(genericCollectionView, positionCollectionView, pcaCollectionView, smallerSize);
    }
  }
  SECTION("Deep copy the ConstView") {
    SECTION("ConstViews with same size") {
      // PortableHostCollection that will host the aggregated columns
      PortableHostCollection<GenericSoA> genericCollection(cms::alpakatools::host(), elems);
      genericCollection.deepCopy(genericConstView);

      GenericSoAConstView genericCollectionConstView = genericCollection.const_view();

      // Check for inequality of memory addresses
      requireDifferentAddresses(genericCollection.const_view(), positionCollectionView, pcaCollectionView);
      requireContentEqual(genericCollectionConstView, positionCollectionView, pcaCollectionView, elems);
    }
    SECTION("ConstViews with different size") {
      const auto smallerSize = elems / 2;
      PortableHostCollection<GenericSoA> genericCollection(cms::alpakatools::host(), smallerSize);
      genericCollection.deepCopy(genericConstView);

      GenericSoAConstView genericCollectionConstView = genericCollection.const_view();

      // Check for inequality of memory addresses
      requireDifferentAddresses(genericCollectionConstView, positionCollectionView, pcaCollectionView);
      requireContentEqual(genericCollectionConstView, positionCollectionView, pcaCollectionView, smallerSize);
    }
  }
}
