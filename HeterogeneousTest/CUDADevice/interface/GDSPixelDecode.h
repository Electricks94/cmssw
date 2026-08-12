#ifndef HeterogeneousTest_CUDADevice_GDSPixelDecode_h
#define HeterogeneousTest_CUDADevice_GDSPixelDecode_h

#include <cstdint>

// Minimal on-device pixel unpacker: the self-contained half of
// RawToDigi_kernel. Decodes each 32-bit word's bit fields and converts to
// ROC-local coordinates. Does NOT do the cabling-map lookup (fedId,link,ROC ->
// DetId) or gain calibration -- those need EventSetup conditions.
//
// Purpose: prove the gathered words really are pixel digis.

namespace gdsdecode {

  struct Digi {
    uint32_t link;    // 1..MAX_LINK
    uint32_t roc;     // 1..MAX_ROC
    uint32_t rocRow;  // 0..79   within the ROC
    uint32_t rocCol;  // 0..51   within the ROC
    uint32_t adc;     // 0..255
    uint32_t fedId;   // from the gather's tag array
  };

  struct DecodeResult {
    Digi* d_digis = nullptr;  // device array, length nWords
    uint32_t nWords = 0;
    uint32_t nValid = 0;      // words decoding to in-range digis
    uint32_t nBadLink = 0;
    uint32_t nBadRoc = 0;
    uint32_t nBadCoord = 0;
    uint32_t nZeroAdc = 0;
  };

  // Caller frees d_digis with cudaFree.
  DecodeResult decodeWords(const uint32_t* d_words,
                           const uint8_t* d_fedIndex,
                           uint32_t nWords,
                           uint32_t minFedId);

}  // namespace gdsdecode

#endif
