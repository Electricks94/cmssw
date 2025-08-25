// Author: Felice Pantaleo (CERN), 2025, felice.pantaleo@cern.ch
#ifndef CommonTools_RecoAlgos_MultiCollectionManager_h
#define CommonTools_RecoAlgos_MultiCollectionManager_h

#include <cassert>
#include <span>
#include <utility>
#include <type_traits>
#include <vector>

#include "DataFormats/Common/interface/RefProd.h"
#include "DataFormats/Portable/interface/MultiSoAViewManager.h"

#include "HeterogeneousCore/AlpakaInterface/interface/CopyToDevice.h"
#include "HeterogeneousCore/AlpakaInterface/interface/CopyToHost.h"

#include "MultiVectorManager.h"

template <typename... Ts>
struct is_vector :std::false_type {};

template <typename... Ts>
struct is_vector< std::vector<Ts...> >:std::true_type {};

template < typename  T>
inline constexpr auto is_vector_v = is_vector<T>::value;

/**
 * @brief Lightweight persistent holder for several `edm::RefProd<Collection>`
 *        objects. The Collection can be an std::vector or an SoA
 *
 * Only the array of `RefProd`s is stored on disk.  No transient caches or
 * synchronisation primitives live inside the class, so it is trivially
 * movable. Consumers obtain a flat view with `makeFlatView()`, which assembles
 * and *returns* a fully‑populated `MultiVectorManager` by value. Using SoAs
 * `makeFlatView()` returns an  `MultiSoAViewManager` by value.
 *
 * This design avoids copy/move issues with `std::once_flag` and makes the type
 * EDM‑wrapper‑friendly while still giving fast, indexed access to the
 * concatenated elements. SoAs that are allocated on accelerators can be used inside
 * a kernel direcly with the `MultiSoAViewManager`.
 */
template <typename Collection, std::size_t N>
class MultiCollectionManager {
public:

  // ---------------- producer‑side constructor ----------------------------------

  template <typename... Args>
  explicit MultiCollectionManager(Args&&... refs) : refProds_{{std::forward<Args>(refs)...}} {
    static_assert(sizeof...(Args) == 0 || sizeof...(Args) == N, "Number of arguments must be equal to N");  
  }

  // ---------------- consumer‑side helpers ------------------------------
  /**
   * @brief Build and return a flat view that spans all referenced collections.
   *
   * The returned `MultiVectorManager` or `MultiSoAViewManager` is independent of `this`, so callers may
   * move or store it locally without keeping the manager alive.
   */
  template<typename T = void>
  [[nodiscard]] auto makeFlatView() const {
    if constexpr (is_vector_v<Collection>) {
      MultiVectorManager<typename Collection::value_type> mv;
      for (auto const& rp : refProds_) {
        auto const& coll = *rp;
        mv.addVector(coll);
      }
      return mv;
    } else {
      return std::apply([](auto const&... vs) {
        if constexpr (std::is_void_v<T>) {
          // Extract the view from a PortableCollection
          return MultiSoAViewManager(vs->const_view()...);
        } else {
          // Extract the view from a PortableMultiCollection
          return MultiSoAViewManager(vs->template const_view<T>()...); 
        }
      }, refProds_);
    }
  }

  const std::array<edm::RefProd< Collection >, N>& refProds() const { return refProds_; }
  
private:
  std::array<edm::RefProd<Collection>, N> refProds_;
};

#endif  // CommonTools_RecoAlgos_MultiCollectionManager_h
