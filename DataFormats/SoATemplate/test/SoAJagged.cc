#include <memory>
#include <tuple>

#define CATCH_CONFIG_MAIN
#include <catch2/catch_all.hpp>

#include "DataFormats/SoATemplate/interface/SoALayout.h"

// TODO: remove later
#include <iostream>

// clang-format off
GENERATE_SOA_LAYOUT(SimpleLayoutTemplate,
  SOA_JAGGED_COLUMN(float, x),
  SOA_COLUMN(float, y))
// clang-format on

using SimpleLayout = SimpleLayoutTemplate<>;
using View = SimpleLayout::View;

TEST_CASE("SoATemplate") {
  // number of elements
  const View::size_type slSize = 10;
  const View::size_type jaggedSize = 20;
  // size in bytes
  const std::size_t slBufferSize = SimpleLayout::computeDataSize(slSize, jaggedSize);
  // memory buffer aligned according to the layout requirements
  std::unique_ptr<std::byte, decltype(std::free) *> slBuffer{
      reinterpret_cast<std::byte *>(aligned_alloc(SimpleLayout::alignment, slBufferSize)), std::free};
  // SoA layout
  SimpleLayout sl{slBuffer.get(), slSize, jaggedSize};



  View slv{sl};

  

  for (View::size_type i = 0; i < slSize; ++i) {
    auto slvi = slv[i];
    slvi.y() = static_cast<float>(i);
  }

  std::cout << "Init y done" << std::endl;

  for (View::size_type i = 0; i < jaggedSize; ++i) {
    auto slvi = slv[i];
    slvi.x() = static_cast<float>(jaggedSize);
  }

  std::cout << "Init x done" << std::endl;


  for (View::size_type i = 0; i < slv.metadata().size()[0]; ++i) {
    auto slvi = slv[i];
    std::cout << "index: " << i << ", x: " << slvi.x() << ", y: " << slvi.y() << std::endl;

  }

  for (View::size_type i = 0; i < slv.metadata().size()[1]; ++i) {
    auto slvi = slv[i];
    std::cout << "index: " << i << ", x: " << slvi.y() << std::endl;

  }

  // TODO implement access for jagged columns and test here


}
