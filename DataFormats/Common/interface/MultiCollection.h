// Author: Felice Pantaleo (CERN), 2025, felice.pantaleo@cern.ch
#ifndef DataFormats_Common_MultiCollection_h
#define DataFormats_Common_MultiCollection_h

#include <cassert>
#include <span>
#include <utility>
#include <type_traits>
#include <vector>

#include "DataFormats/Common/interface/RefProd.h"
#include "DataFormats/Common/interface/MultiSpan.h"
#include "DataFormats/Portable/interface/MultiView.h"
#include "DataFormats/Portable/interface/MultiBlocksView.h"

#include "HeterogeneousCore/AlpakaInterface/interface/CopyToDevice.h"
#include "HeterogeneousCore/AlpakaInterface/interface/CopyToHost.h"


template <typename... Ts>
struct is_vector :std::false_type {};

template <typename... Ts>
struct is_vector< std::vector<Ts...> >:std::true_type {};

template < typename  T>
inline constexpr auto is_vector_v = is_vector<T>::value;

/**
 * @brief Lightweight persistent holder for several `edm::RefProd<Collection>`
 *        objects. The Collection can be an std::vector or a PortableCollection
 *
 * Only the array of `RefProd`s is stored on disk.  No transient caches or
 * synchronisation primitives live inside the class, so it is trivially
 * movable. Consumers obtain a flat view with `makeFlatView()`, which assembles
 * and *returns* a fully‑populated `MultiSpan` by value. Using PortableCollections,
 * `makeFlatView()` returns an  `MultiView` by value.
 *
 * This design avoids copy/move issues with `std::once_flag` and makes the type
 * EDM‑wrapper‑friendly while still giving fast, indexed access to the
 * concatenated elements. SoAs that are allocated on accelerators can be used inside
 * a kernel direcly with the `MultiView`.
 */
template <typename Collection>
class MultiCollection {
public:

  MultiCollection() = default;

  explicit MultiCollection(std::initializer_list<edm::RefProd<Collection>> refs) : refProds_{refs} {}

  // ---------------- producer‑side API ----------------------------------
  void addCollection(edm::RefProd<Collection> const& ref) { refProds_.push_back(ref); }

  // ---------------- consumer‑side helpers ------------------------------
  /**
   * @brief Build and return a flat view that spans all referenced collections.
   *
   * The returned `MultiSpan` or `MultiView` is independent of `this`, so callers may
   * move or store it locally without keeping the manager alive.
   */
  template<typename T = void>
  [[nodiscard]] auto makeFlatView() const {
    if constexpr (is_vector_v<Collection>) {
      edm::MultiSpan<typename Collection::value_type> ms;
      for (auto const& rp : refProds_) {
        auto const& coll = *rp;
        ms.add(coll);
      }
      return ms;
    } else {
        MultiBlocksView<typename Collection::ConstView> soaViewManager;
        for(const auto& rp : refProds_){
          soaViewManager.addView(rp->const_view());
        }
        return soaViewManager;
      } 
    }

  [[nodiscard]] const std::vector<edm::RefProd<Collection>>& refProds() const { return refProds_; }
  
private:
  std::vector<edm::RefProd<Collection>> refProds_;
};

#endif  // DataFormats_Common_MultiCollection_h
