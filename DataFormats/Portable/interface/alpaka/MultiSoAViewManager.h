#ifndef DataFormats_Portable_interface_alpaka_MultiSoAViewManager_h
#define DataFormats_Portable_interface_alpaka_MultiSoAViewManager_h

#include <array>
#include <concepts>
#include <cstddef>
#include <type_traits>

#include <alpaka/alpaka.hpp>

#include "FWCore/Utilities/interface/CMSUnrollLoop.h"
#include "HeterogeneousCore/AlpakaInterface/interface/config.h"

namespace ALPAKA_ACCELERATOR_NAMESPACE {

  template <typename... Views>
  requires(sizeof...(Views) > 0 && (std::same_as<Views, Views> && ...))
  class MultiSoAViewManager {
  public:
    using View = typename std::common_type_t<Views...>;
    using Element = typename View::element;
    static constexpr std::size_t N = sizeof...(Views);

    MultiSoAViewManager(const Views&... views) : views_{{views...}}, offsets_{} {
      std::size_t i = 0;
      ((offsets_[i] = totalSize_, totalSize_ += views.metadata().size(), ++i), ...);
    }

    ALPAKA_FN_HOST_ACC Element operator[](const std::size_t globalIndex) {
      assert(globalIndex < totalSize_ && "Global index out of range");

      const std::size_t vi = viewIndex(globalIndex);
      const std::size_t li = globalIndex - offsets_[vi];
      return views_[vi][li];
    }

    ALPAKA_FN_HOST_ACC View getView(const std::size_t globalIndex) {
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
    std::array<View, N> views_;
    std::array<std::size_t, N> offsets_;
    std::size_t totalSize_{0};
  };

}  // namespace ALPAKA_ACCELERATOR_NAMESPACE

#endif  // DataFormats_Portable_interface_alpaka_MultiSoAViewManager_h
