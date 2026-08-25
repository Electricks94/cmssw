#ifndef HeterogeneousTest_CUDADevice_GDSPixelDecode_h
#define HeterogeneousTest_CUDADevice_GDSPixelDecode_h

#include <cstdint>

// Minimal on-device pixel unpacker: the self-contained half of RawToDigi_kernel.
// Decodes each 32-bit word's bit fields into ROC-local coordinates. Does NOT do
// the cabling-map lookup (fedId,link,ROC -> DetId) or gain calibration, which
// need EventSetup conditions.
//
// Scratch memory lives in a caller-owned Workspace, reused across events.

namespace gdsdecode {

  struct Digi {
    uint32_t link;    // 1..48
    uint32_t roc;     // 1..8
    uint32_t rocRow;  // 0..79 within the ROC
    uint32_t rocCol;  // 0..51 within the ROC
    uint32_t adc;     // 0..255
    uint32_t fedId;
  };

  struct Workspace {
    Digi* d_digis = nullptr;       // maxWords
    uint32_t* d_counters = nullptr;  // 5
    uint32_t maxWords = 0;
  };

  void allocateWorkspace(Workspace& ws, uint32_t maxWords);
  void freeWorkspace(Workspace& ws);

  // d_digis points INTO the workspace and is not owned by the result
  struct DecodeResult {
    Digi* d_digis = nullptr;
    uint32_t nWords = 0;
    uint32_t nValid = 0;
    uint32_t nBadLink = 0;
    uint32_t nBadRoc = 0;
    uint32_t nBadCoord = 0;
    uint32_t nZeroAdc = 0;
  };

  DecodeResult decodeWords(const uint32_t* d_words,
                           const uint8_t* d_fedIndex,
                           uint32_t nWords,
                           uint32_t minFedId,
                           Workspace& ws);

}  // namespace gdsdecode

#endif
