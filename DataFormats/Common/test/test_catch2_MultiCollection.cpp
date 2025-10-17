#include <algorithm>
#include <cassert>
#include <cmath>
#include <numeric>
#include <vector>
#include <catch.hpp>

#include "DataFormats/Common/interface/MultiCollection.h"
#include "DataFormats/Common/interface/RefProd.h"

TEST_CASE("MultiCollection basic test", "[MultiCollection]") {
  edm::MultiCollection<std::vector<int>> emptyMC;
  edm::MultiCollection<std::vector<int>> mc;

  std::vector<int> a = {1, 2, 3};
  std::vector<int> b = {4, 5};
  std::vector<int> c;

  mc.add(edm::RefProd<std::vector<int>>(&a));
  mc.add(edm::RefProd<std::vector<int>>(&b));
  mc.add(edm::RefProd<std::vector<int>>(&c));

  edm::MultiCollection<std::vector<int>> mc2({edm::RefProd<std::vector<int>>(&a),
                                              edm::RefProd<std::vector<int>>(&b),
                                              edm::RefProd<std::vector<int>>(&c),
                                              edm::RefProd<std::vector<int>>(&a),
                                              edm::RefProd<std::vector<int>>(&b),
                                              edm::RefProd<std::vector<int>>(&c)});

  SECTION("Check empty MultiCollection") {
    auto ms = emptyMC.makeFlatView();
    REQUIRE(ms.size() == 0);
  }


  SECTION("Check correctness of produced MultiSpan") {
    auto ms = mc.makeFlatView();
    REQUIRE(ms.size() == 5);
    REQUIRE(ms[0] == 1);
    REQUIRE(ms[1] == 2);
    REQUIRE(ms[2] == 3);
    REQUIRE(ms[3] == 4);
    REQUIRE(ms[4] == 5);
    REQUIRE(ms.globalIndex(0, 0) == 0);
    REQUIRE(ms.globalIndex(0, 2) == 2);
    REQUIRE(ms.globalIndex(1, 0) == 3);
    REQUIRE(ms.globalIndex(1, 1) == 4);
    auto [span0, local0] = ms.spanAndLocalIndex(0);
    REQUIRE(span0 == 0);
    REQUIRE(local0 == 0);

    auto [span1, local1] = ms.spanAndLocalIndex(4);
    REQUIRE(span1 == 1);
    REQUIRE(local1 == 1);
  }

  SECTION("Check correctness of produced MultiSpan through mc2") {
    auto ms = mc2.makeFlatView();
    REQUIRE(ms.size() == 10);
    REQUIRE(ms[0] == 1);
    REQUIRE(ms[1] == 2);
    REQUIRE(ms[2] == 3);
    REQUIRE(ms[3] == 4);
    REQUIRE(ms[4] == 5);
    REQUIRE(ms[5] == 1);
    REQUIRE(ms[6] == 2);
    REQUIRE(ms[7] == 3);
    REQUIRE(ms[8] == 4);
    REQUIRE(ms[9] == 5);
    REQUIRE(ms.globalIndex(0, 0) == 0);
    REQUIRE(ms.globalIndex(0, 2) == 2);
    REQUIRE(ms.globalIndex(1, 0) == 3);
    REQUIRE(ms.globalIndex(1, 1) == 4);
    REQUIRE(ms.globalIndex(3, 0) == 8);
    REQUIRE_THROWS_AS(ms.globalIndex(3, 2), std::out_of_range);

    auto [span0, local0] = ms.spanAndLocalIndex(0);
    REQUIRE(span0 == 0);
    REQUIRE(local0 == 0);

    auto [span1, local1] = ms.spanAndLocalIndex(9);
    REQUIRE(span1 == 3);
    REQUIRE(local1 == 1);
  }

  SECTION("Check ref products stored in MultiCollection") {
    auto const& refProds = mc.refProds();
    REQUIRE(refProds.size() == 3);
    REQUIRE(refProds[0]->size() == a.size());
    REQUIRE(refProds[1]->size() == b.size());
    REQUIRE(refProds[2]->size() == c.size());
  }

}
