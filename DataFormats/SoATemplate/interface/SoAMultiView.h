#ifndef DataFormats_Portable_interface_SoAMultiView_h
#define DataFormats_Portable_interface_SoAMultiView_h

#include <array>
#include <concepts>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <type_traits>

#include "SoACommon.h"

/**
 * @brief Aggregates multiple views into a single combined view, similar to `MultiVectorManager`.
 *
 * `SoAMultiView` stores multiple views as references within an `std::array`, 
 * accompanied by an offset array to enable access via a global index. 
 * Thanks to the use of `std::array`, instances of `SoAMultiView` can be passed 
 * directly by value to kernels without requiring device memory copies.
 *
 * This manager does not own or copy the underlying data; instead, it maintains lightweight 
 * references to existing views, minimizing memory overhead and construction cost.
 * However, since the underlying SoA memories are not contiguous, cacheline inefficiencies 
 * may arise. Therefore, `SoAMultiView` is best suited for use with large SoA views 
 * where such overhead is amortized.
 *
 * To ensure clarity and performance, SoA views must be provided explicitly through the 
 * constructor—no dynamic addition of views is supported.
 */

template <typename ConstView, uint8_t MaxSize = 3>
class SoAMultiView {
public:
  using ConstElement = typename ConstView::const_element;

  SoAMultiView() = default;

  template <typename Collections, typename Getter>
  SoAMultiView(const Collections& collections, Getter getter) {
    std::size_t offset = 0;
    n_ = 0;

    for (const auto& collection : collections) {
      assert(n_ < MaxSize && "Exceeded maximum number of views");

      views_[n_] = getter(collection);
      offsets_[n_] = offset;

      offset += views_[n_].metadata().size();
      ++n_;
    }

    totalSize_ = offset;
  }

  const SOA_HOST_DEVICE ConstElement operator[](const std::size_t globalIndex) const {
    assert(globalIndex < totalSize_ && "Global index out of range");

    const std::size_t vi = viewIndex(globalIndex);
    const std::size_t li = globalIndex - offsets_[vi];
    return views_[vi][li];
  }

  template <typename Func, typename ReduceOp>
  SOA_HOST_DEVICE auto getScalar(Func func, ReduceOp reduceOp) {
    auto result = func(views_[0]);

    for (std::size_t i = 1; i < n_; ++i) {
      result = reduceOp(result, func(views_[i]));
    }

    return result;
  }

  template <typename Func>
  SOA_HOST_DEVICE auto getScalar(Func func) {
    return func(views_[0]);
  }

  SOA_HOST_DEVICE ConstView getView(const std::size_t globalIndex) const {
    assert(globalIndex < totalSize_ && "Global index out of range");

    const std::size_t vi = viewIndex(globalIndex);
    return views_[vi];
  }

  SOA_HOST_DEVICE SOA_INLINE std::size_t viewIndex(const std::size_t globalIndex) const {
    std::size_t result = 0;

    for (std::size_t i = 0; i < n_; ++i) {
      result = (globalIndex >= offsets_[i]) ? i : result;
    }

    return result;
  }

  SOA_HOST_DEVICE SOA_INLINE std::size_t getLocalIndex(const std::size_t globalIndex) const {
    const std::size_t vi = viewIndex(globalIndex);
    const std::size_t li = globalIndex - offsets_[vi];

    return li;
  }

  SOA_HOST_DEVICE SOA_INLINE std::size_t size() const { return totalSize_; }

private:
  std::array<ConstView, MaxSize> views_;
  std::array<std::size_t, MaxSize> offsets_;
  std::size_t totalSize_{0};

  std::size_t n_{0};
};

#endif  // DataFormats_Portable_interface_SoAMultiView_h
