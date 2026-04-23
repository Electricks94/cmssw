#ifndef DataFormats_Portable_interface_SoAMultiView_h
#define DataFormats_Portable_interface_SoAMultiView_h

#include <array>
#include <cassert>
#include <cstdint>

#include "SoACommon.h"

/**
 * @brief Aggregates multiple ConstViews into a single combined view.
 *
 * `SoAMultiView` stores multiple ConstViews within an `std::array`, 
 * accompanied by an offset array to enable access via a global index. 
 * An `SoAMultiView` can be passed directly by value to kernels without 
 * requiring device memory copies.
 *
 * Since the underlying SoA memories are not contiguous, cacheline inefficiencies 
 * may arise. Therefore, `SoAMultiView` is best suited for use with ConstViews, 
 * when the underlying buffer is large enough to amortize the overhead of 
 * non-contiguous access patterns when iterating over all elements.
 *
 */

template <typename ConstView, int MaxSize = 3>
class SoAMultiView {
public:
  using ConstElement = typename ConstView::const_element;
  using size_type = cms::soa::size_type;

  SoAMultiView() = default;

  template <typename Collections, typename Getter>
  explicit SoAMultiView(const Collections& collections, Getter getter) {
    for (const auto& collection : collections) {
      assert(n_ < MaxSize && "Exceeded maximum number of views");

      views_[n_] = getter(collection);
      offsets_[n_] = totalSize_;
      totalSize_ += static_cast<size_type>(views_[n_].metadata().size());
      n_++;
    }
  }

  SOA_HOST_DEVICE SOA_INLINE ConstElement operator[](size_type globalIndex) const {
    if (globalIndex >= totalSize_ or globalIndex < 0) {
      SOA_THROW_OUT_OF_RANGE("Out of range index in SoAMultiView::operator[]", globalIndex, totalSize_)
    }

    const size_type vi = viewIndex(globalIndex);
    const size_type li = globalIndex - offsets_[vi];
    return views_[vi][li];
  }

  SOA_HOST_DEVICE SOA_INLINE ConstView viewAt(size_type globalIndex) const {
    if (globalIndex >= totalSize_ or globalIndex < 0) {
      SOA_THROW_OUT_OF_RANGE("Out of range index in SoAMultiView::viewAt()", globalIndex, totalSize_)
    }
    return views_[viewIndex(globalIndex)];
  }

  template <typename Func, typename ReduceOp>
  SOA_HOST_DEVICE auto getScalar(Func func, ReduceOp reduceOp) {
    auto result = func(views_[0]);

    for (size_type i = 1; i < n_; ++i) {
      result = reduceOp(result, func(views_[i]));
    }

    return result;
  }

  template <typename Func>
  SOA_HOST_DEVICE auto getScalar(Func func) {
    return func(views_[0]);
  }

  SOA_HOST_DEVICE SOA_INLINE ConstView view(size_type i) const {
    if (i >= n_ or i < 0) {
      SOA_THROW_OUT_OF_RANGE("Out of range index in SoAMultiView::view()", i, n_)
    }
    return views_[i];
  }

  SOA_HOST_DEVICE SOA_INLINE size_type size() const { return totalSize_; }
  SOA_HOST_DEVICE SOA_INLINE size_type numViews() const { return n_; }

private:
  SOA_HOST_DEVICE SOA_INLINE size_type viewIndex(size_type globalIndex) const {
    size_type result = 0;
    for (size_type i = 1; i < n_; ++i)
      result = (globalIndex >= offsets_[i]) ? i : result;
    return result;
  }

  std::array<ConstView, MaxSize> views_;
  std::array<size_type, MaxSize> offsets_;
  size_type totalSize_{0};

  size_type n_{0};
};

#endif  // DataFormats_Portable_interface_SoAMultiView_h
