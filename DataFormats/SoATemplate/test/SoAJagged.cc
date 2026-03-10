#include <memory>
#include <tuple>

#define CATCH_CONFIG_MAIN
#include <catch2/catch_all.hpp>

#include "DataFormats/SoATemplate/interface/SoALayout.h"

// clang-format off
GENERATE_SOA_LAYOUT(SimpleLayoutTemplate,
  SOA_JAGGED_COLUMN(float, x),
  SOA_COLUMN(float, y))
// clang-format on

using SimpleLayout = SimpleLayoutTemplate<>;

TEST_CASE("SoATemplate") {
  // number of elements
  const std::size_t slSize = 10;
  const std::size_t jaggedSize = 20;
  // size in bytes
  const std::size_t slBufferSize = SimpleLayout::computeDataSize(slSize, jaggedSize);
  // memory buffer aligned according to the layout requirements
  std::unique_ptr<std::byte, decltype(std::free) *> slBuffer{
      reinterpret_cast<std::byte *>(aligned_alloc(SimpleLayout::alignment, slBufferSize)), std::free};
  // SoA layout
  SimpleLayout sl{slBuffer.get(), slSize, jaggedSize};



  SimpleLayout::View slv{sl};

  // TODO implement access for jagged columns and test here


}
