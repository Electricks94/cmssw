#ifndef DataFormats_Portable_interface_alpaka_MultiSoAViewManager_h
#define DataFormats_Portable_interface_alpaka_MultiSoAViewManager_h

#include <array>
#include <concepts>
#include <cstddef>
#include <type_traits>

#include <alpaka/alpaka.hpp>

#include "FWCore/Utilities/interface/CMSUnrollLoop.h"
#include "HeterogeneousCore/AlpakaInterface/interface/config.h"



template <typename... ConstViews>
requires(sizeof...(ConstViews) > 0 && (std::same_as<ConstViews, ConstViews> && ...))
class MultiSoAViewManager {
public:
  using ConstView = typename std::common_type_t<ConstViews...>;
  using ConstElement = typename ConstView::const_element;
  static constexpr std::size_t N = sizeof...(ConstViews);

  MultiSoAViewManager(const ConstViews&... views) : views_{{views...}}, offsets_{} {
    std::size_t i = 0;
    ((offsets_[i] = totalSize_, totalSize_ += views.metadata().size(), ++i), ...);
  }

  const ALPAKA_FN_HOST_ACC ConstElement operator[](const std::size_t globalIndex) const {
    assert(globalIndex < totalSize_ && "Global index out of range");

    const std::size_t vi = viewIndex(globalIndex);
    const std::size_t li = globalIndex - offsets_[vi];
    return views_[vi][li];
  }

  ALPAKA_FN_HOST_ACC ConstView getView(const std::size_t globalIndex) const {
    assert(globalIndex < totalSize_ && "Global index out of range");

    const std::size_t vi = viewIndex(globalIndex);
    return views_[vi];
  }

  ALPAKA_FN_HOST_ACC ALPAKA_FN_INLINE std::size_t viewIndex(const std::size_t globalIndex) const {
    std::size_t result = 0;

    CMS_UNROLL_LOOP
    for (std::size_t i = 0; i < N; ++i) {
      result = (globalIndex >= offsets_[i]) ? i : result;
    }

    return result;
  }

  ALPAKA_FN_HOST_ACC ALPAKA_FN_INLINE std::size_t getLocalIndex(const std::size_t globalIndex) const {
    const std::size_t vi = viewIndex(globalIndex);
    const std::size_t li = globalIndex - offsets_[vi];

    return li;
  }

  ALPAKA_FN_HOST_ACC ALPAKA_FN_INLINE std::size_t size() const { return totalSize_; }

private:
  std::array<ConstView, N> views_;
  std::array<std::size_t, N> offsets_;
  std::size_t totalSize_{0};
  // std::size_t addedViews_{0};
};

#endif  // DataFormats_Portable_interface_alpaka_MultiSoAViewManager_h
