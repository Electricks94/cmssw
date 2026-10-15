#ifndef RecoTracker_PixelSeeding_plugins_alpaka_CAPixelDoubletsAlgos_h
#define RecoTracker_PixelSeeding_plugins_alpaka_CAPixelDoubletsAlgos_h

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>

#include <alpaka/alpaka.hpp>

#include "DataFormats/Math/interface/approx_atan2.h"
#include "DataFormats/SiPixelClusterSoA/interface/ClusteringConstants.h"
#include "DataFormats/TrackingRecHitSoA/interface/TrackingRecHitsSoA.h"
#include "Geometry/CommonTopologies/interface/SimplePixelTopology.h"
#include "HeterogeneousCore/AlpakaInterface/interface/VecArray.h"
#include "HeterogeneousCore/AlpakaInterface/interface/config.h"
#include "HeterogeneousCore/AlpakaInterface/interface/workdivision.h"
#include "RecoTracker/PixelSeeding/interface/CAGeometrySoA.h"

#include "CACell.h"
#include "CAStructures.h"
#include "CAHitNtupletGeneratorKernels.h"

// #define GPU_DEBUG
// #define DOUBLETS_DEBUG
// #define CA_WARNINGS

namespace ALPAKA_ACCELERATOR_NAMESPACE::caPixelDoublets {
  using namespace cms::alpakatools;
  using namespace ::caStructures;
  using namespace ::reco;

  using HitToCell = GenericContainer;

  template <typename TrackerTraits>
  using PhiBinner = PhiBinnerT<TrackerTraits>;
  //Move this ^ definition in CAStructures maybe

  ALPAKA_FN_ACC ALPAKA_FN_INLINE bool moduleIsOuterLadderPhase1(int const moduleId) {
    return (0 == (moduleId / 8) % 2);
  }
  ALPAKA_FN_ACC ALPAKA_FN_INLINE bool moduleIsOuterLadderPhase2(int const moduleId) {
    return (0 != (moduleId / 18) % 2);
  }

  template <typename TrackerTraits>
  ALPAKA_FN_ACC ALPAKA_FN_INLINE bool moduleIsOuterLadder(const int moduleId);

  template <>
  ALPAKA_FN_ACC ALPAKA_FN_INLINE bool moduleIsOuterLadder<pixelTopology::Phase1>(int const moduleId) {
    return moduleIsOuterLadderPhase1(moduleId);
  }

  template <>
  ALPAKA_FN_ACC ALPAKA_FN_INLINE bool moduleIsOuterLadder<pixelTopology::HIonPhase1>(int const moduleId) {
    return moduleIsOuterLadderPhase1(moduleId);
  }

  template <>
  ALPAKA_FN_ACC ALPAKA_FN_INLINE bool moduleIsOuterLadder<pixelTopology::Phase2>(int const moduleId) {
    return moduleIsOuterLadderPhase2(moduleId);
  }

  template <>
  ALPAKA_FN_ACC ALPAKA_FN_INLINE bool moduleIsOuterLadder<pixelTopology::Phase2OT>(int const moduleId) {
    return moduleIsOuterLadderPhase2(moduleId);
  }

  template <typename TrackerTraits, alpaka::concepts::Acc TAcc>
  ALPAKA_FN_ACC ALPAKA_FN_INLINE bool zSizeCut(const TAcc& acc,
                                               const auto element_i,
                                               const auto element_o,
                                               ::reco::CALayersSoAConstView ll,
                                               AlgoParams const& params) {
    const uint32_t mi = element_i.detectorIndex();
    const auto first_forward = ll.layerStarts()[4];
    const auto first_bpix2 = ll.layerStarts()[1];
    bool innerB1 = mi < first_bpix2;
    bool isOuterLadder = moduleIsOuterLadder<TrackerTraits>(mi);
    auto mes = (!innerB1) || isOuterLadder ? element_i.clusterSizeY() : -1;
    if (mes < 0)
      return false;

    const uint32_t mo = element_o.detectorIndex();
    auto so = element_o.clusterSizeY();

    auto dz = element_i.zGlobal() - element_o.zGlobal();
    auto dr = element_i.rGlobal() - element_o.rGlobal();

    auto innerBarrel = mi < first_forward;
    auto onlyBarrel = mo < first_forward;
    if (not innerBarrel and not onlyBarrel)
      return false;
    auto dy = innerB1 ? params.maxDYsize12_ : params.maxDYsize_;
    return onlyBarrel
               ? so > 0 && std::abs(so - mes) > dy
               : innerBarrel && std::abs(mes - int(std::abs(dz / dr) * params.dzdrFact_ + 0.5f)) > params.maxDYPred_;
  }

  template <typename TrackerTraits, alpaka::concepts::Acc TAcc>
  ALPAKA_FN_ACC ALPAKA_FN_INLINE bool clusterCut(const TAcc& acc,
                                                 const auto element,
                                                 ::reco::CALayersSoAConstView ll,
                                                 AlgoParams const& params) {
    const uint32_t mi = element.detectorIndex();
    const auto first_bpix2 = ll.layerStarts()[1];
    const auto first_bpix3 = ll.layerStarts()[2];
    bool innerB1orB2 = mi < ll.layerStarts()[2];
    if (!innerB1orB2)
      return false;

    bool innerB1 = mi < first_bpix2;

    bool isOuterLadder = moduleIsOuterLadder<TrackerTraits>(mi);
    auto mes = (!innerB1) || isOuterLadder ? element.clusterSizeY() : -1;

    if (innerB1)  // B1
      if (mes > 0 && mes < params.minYsizeB1_)
        return true;  // only long cluster  (5*8)
    bool innerB2 = (mi >= first_bpix2) && (mi < first_bpix3);
    if (innerB2)  // B2 and F1
      if (mes > 0 && mes < params.minYsizeB2_)
        return true;

    return false;
  }

  template <typename TrackerTraits, alpaka::concepts::Acc TAcc>
  ALPAKA_FN_ACC ALPAKA_FN_INLINE void doubletsFromHisto(const TAcc& acc,
                                                        uint32_t maxNumOfDoublets,
                                                        CACell<TrackerTraits>* cells,
                                                        uint32_t* nCells,
                                                        caStructures::HitsMultiView hh,
                                                        ::reco::CAGraphSoAConstView cc,
                                                        ::reco::CALayersSoAConstView ll,
                                                        uint32_t const* __restrict__ offsets,
                                                        PhiBinner<TrackerTraits> const* phiBinner,
                                                        HitToCell* outerHitHisto,
                                                        AlgoParams const& params) {
    const bool doClusterCut = params.minYsizeB1_ > 0 or params.minYsizeB2_ > 0;
    const bool doZSizeCut = params.maxDYsize12_ > 0 or params.maxDYsize_ > 0 or params.maxDYPred_ > 0;

    const uint32_t nPairs = cc.metadata().size();
    using PhiHisto = PhiBinner<TrackerTraits>;
    ALPAKA_ASSERT_ACC(offsets);

    auto layerSize = [=](uint8_t li) { return offsets[li + 1] - offsets[li]; };

    // nPairs for the OT-extended CA is 73.
    // If it should become much bigger than 64, consider using a block-wide parallel prefix scan,
    // e.g. see  https://nvlabs.github.io/cub/classcub_1_1_warp_scan.html
    auto& innerLayerCumulativeSize = alpaka::declareSharedVar<uint32_t[TrackerTraits::nPairs], __COUNTER__>(acc);
    auto& ntot = alpaka::declareSharedVar<uint32_t, __COUNTER__>(acc);

    if (cms::alpakatools::once_per_block(acc)) {
      innerLayerCumulativeSize[0] = layerSize(cc.graph()[0][0]);
      for (uint32_t i = 1; i < nPairs; ++i) {
        innerLayerCumulativeSize[i] = innerLayerCumulativeSize[i - 1] + layerSize(cc.graph()[i][0]);
      }
      ntot = innerLayerCumulativeSize[nPairs - 1];
    }
    alpaka::syncBlockThreads(acc);

    // declared outside the loop, as it cannot go backward
    uint32_t pairLayerId = 0;

    // outermost parallel loop, using all grid elements along the slower dimension (Y or 0 in a 2D grid)
    for (uint32_t j : cms::alpakatools::uniform_elements_y(acc, ntot)) {
      // move to lower_bound ?
      while (j >= innerLayerCumulativeSize[pairLayerId++])
        ;
      --pairLayerId;

      ALPAKA_ASSERT_ACC(pairLayerId < nPairs);
      ALPAKA_ASSERT_ACC(j < innerLayerCumulativeSize[pairLayerId]);
      ALPAKA_ASSERT_ACC(0 == pairLayerId || j >= innerLayerCumulativeSize[pairLayerId - 1]);

      uint8_t inner = cc.graph()[pairLayerId][0];
      uint8_t outer = cc.graph()[pairLayerId][1];
      ALPAKA_ASSERT_ACC(outer > inner);

      auto hoff = PhiHisto::histOff(outer);
      auto i = (0 == pairLayerId) ? j : j - innerLayerCumulativeSize[pairLayerId - 1];
      i += offsets[inner];

      const auto element_i = hh[i];

      ALPAKA_ASSERT_ACC(i >= offsets[inner]);
      ALPAKA_ASSERT_ACC(i < offsets[inner + 1]);
      // found hit corresponding to our worker thread, now do the job
      if (element_i.detectorIndex() > ll.layerStarts()[ll.metadata().size() - 1]) {  //TODO use cc
        continue;  // invalid
      }

      /* maybe clever, not effective when zoCut is on
      auto bpos = (mi%8)/4;  // if barrel is 1 for z>0
      auto fpos = (outer>3) & (outer<7);
      if ( ((inner<3) & (outer>3)) && bpos!=fpos) continue;
      */

      auto zi = element_i.zGlobal();
      auto ri = element_i.rGlobal();

      // cut on inner coordinate (z or r depending on layer)
      auto valInner = ll.isBarrel()[inner] ? zi : ri;
      if (valInner < cc.minInner()[pairLayerId] || valInner > cc.maxInner()[pairLayerId]) {
        continue;
      }

      if (doClusterCut && outer > pixelTopology::last_barrel_layer &&
          clusterCut<TrackerTraits, TAcc>(acc, element_i, ll, params)) {
        continue;
      }

      auto mep = element_i.iphi();

      // all cuts: true if fails
      auto ptcut = [&](const auto element, int16_t idphi) {
        // ptCut already converted to minRadius2T4 in CAHitNtupletGenerator.cc
        auto ro = element.rGlobal();
        auto dphi = short2phi(idphi);
        return dphi * dphi * (cc.ptCuts()[pairLayerId] - ri * ro) > (ro - ri) * (ro - ri);
      };
      auto z0cutoff = [&](const auto element) {
        auto zo = element.zGlobal();
        auto ro = element.rGlobal();
        auto dr = ro - ri;
        return dr > cc.maxDR()[pairLayerId] || dr < 0 || std::abs((zi * ro - ri * zo)) > params.cellZ0Cut_ * dr;
      };

      auto iphicut = cc.phiCuts()[pairLayerId];

      auto kl = PhiHisto::bin(int16_t(mep - iphicut));
      auto kh = PhiHisto::bin(int16_t(mep + iphicut));
      auto incr = [](auto& k) { return k = (k + 1) % PhiHisto::nbins(); };

      auto khh = kh;
      incr(khh);
      for (auto kk = kl; kk != khh; incr(kk)) {
        //#ifdef GPU_DEBUG
        //        if (kk != kl && kk != kh)
        //          nmin += phiBinner->size(kk + hoff);
        //#endif

        auto const* __restrict__ p = phiBinner->begin(kk + hoff);
        auto const* __restrict__ e = phiBinner->end(kk + hoff);
        auto const maxpIndex = e - p;

        // innermost parallel loop, using the block elements along the faster dimension (X or 1 in a 2D grid)
        for (uint32_t pIndex : cms::alpakatools::independent_group_elements_x(acc, maxpIndex)) {
          // FIXME implement alpaka::ldg and use it here? or is it const* __restrict__ enough?
          auto oi = p[pIndex];
          const auto element_oi = hh[oi];
          ALPAKA_ASSERT_ACC(oi >= offsets[outer]);
          ALPAKA_ASSERT_ACC(oi < offsets[outer + 1]);
          auto mo = element_oi.detectorIndex();

          // invalid
          if (mo > pixelClustering::maxNumModules) {  //FIXME use cc?
            continue;
          }

          auto zo = element_oi.zGlobal();
          auto ro = element_oi.rGlobal();

          // cut on outer coordinate (z or r depending on layer)
          auto valOuter = ll.isBarrel()[outer] ? zo : ro;
          if (valOuter < cc.minOuter()[pairLayerId] || valOuter > cc.maxOuter()[pairLayerId]) {
            continue;
          }

          auto dz = zo - zi;

          // cut on signed dz
          if (dz < cc.minDZ()[pairLayerId] || dz > cc.maxDZ()[pairLayerId]) {
            continue;
          }

          if (params.cellZ0Cut_ > 0. && z0cutoff(element_oi)) {
            continue;
          }

          auto mop = element_oi.iphi();
          uint16_t idphi = std::min(std::abs(int16_t(mop - mep)), std::abs(int16_t(mep - mop)));

          if (idphi > iphicut) {
            continue;
          }
          if (doZSizeCut && zSizeCut<TrackerTraits, TAcc>(acc, element_i, element_oi, ll, params)) {
            continue;
          }

          if (cc.ptCuts()[pairLayerId] > 0. && ptcut(element_oi, idphi)) {
            continue;
          }

          auto ind = alpaka::atomicAdd(acc, nCells, 1u, alpaka::hierarchy::Blocks{});
          if (ind >= maxNumOfDoublets or ind >= uint32_t(outerHitHisto->capacity())) {
            alpaka::atomicSub(acc, nCells, 1u, alpaka::hierarchy::Blocks{});
            break;
          }

          outerHitHisto->count(acc, oi - hh.view(0).offsetBPIX2());
          cells[ind].init(hh, pairLayerId, inner, outer, i, oi);
        }
      }
    }  // loop in block...
  }

}  // namespace ALPAKA_ACCELERATOR_NAMESPACE::caPixelDoublets

#endif  // RecoTracker_PixelSeeding_plugins_alpaka_CAPixelDoubletsAlgos_h
