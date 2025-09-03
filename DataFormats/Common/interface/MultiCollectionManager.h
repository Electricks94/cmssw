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

#include "DataFormats/HGCalReco/interface/MultiVectorManager.h"

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
template <typename Collection>
class MultiCollectionManager {
public:

  MultiCollectionManager() = default;

  explicit MultiCollectionManager(std::initializer_list<edm::RefProd<Collection>> refs) : refProds_{refs} {}

  // ---------------- producer‑side API ----------------------------------
  void addCollection(edm::RefProd<Collection> const& ref) { refProds_.push_back(ref); }

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
      if constexpr (std::is_void_v<T>) {
        MultiSoAViewManager<typename Collection::ConstView> soaViewManager;
        for(const auto& rp : refProds_){
          soaViewManager.addView(rp->const_view());
        }
        return soaViewManager;
      } else {
        MultiSoAViewManager<typename T::ConstView> soaViewManager;
        for(const auto& rp : refProds_){
          soaViewManager.addView(rp->template const_view<T>());
        }
        return soaViewManager;
      }
    }
  }

  [[nodiscard]] const std::vector<edm::RefProd<Collection>>& refProds() const { return refProds_; }
  
private:
  std::vector<edm::RefProd<Collection>> refProds_;
};

#endif  // CommonTools_RecoAlgos_MultiCollectionManager_h
