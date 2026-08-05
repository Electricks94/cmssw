#ifndef FRDScan_h
#define FRDScan_h

#include <cstdint>

// Output of the on-device scan of one raw chunk. Everything here is an INDEX
// into the device chunk buffer, not a copy of any payload bytes. The chunk
// stays put; this describes where each event and each FED fragment live in it.

namespace frdscan {

  // one entry per event found in the chunk
  struct EventRecord {
    uint64_t headerOffset;   // byte offset of the FRD event header within the chunk
    uint64_t payloadOffset;  // byte offset of the FED payload (headerOffset + FRD header size)
    uint32_t payloadSize;    // FED payload size in bytes (FRD eventSize_, excludes the header)
    uint32_t run;
    uint32_t lumi;
    uint32_t event;
    uint32_t flags;          // FRD event flags (bit 0 = isGenData)
    uint32_t nFeds;          // number of FED fragments in this event
    uint32_t fedIndexBase;   // start index of this event's fragments in the compact FedEntry array
    uint32_t truncated;      // 1 if the FED walk aborted early (malformed length); 0 if clean
  };

  // one entry per FED fragment (per event), packed into a single compact array
  struct FedEntry {
    uint32_t offset;  // byte offset of the fragment (its FEDHeader) within the chunk
    uint32_t size;    // fragment size in bytes (header + detector data + trailer)
    uint32_t fedId;   // FED source id (uint32 to keep the struct naturally aligned, no padding)
  };

  // Result of a scan. All three device arrays are owned by the caller (cudaFree).
  // d_feds has length totalFeds (the exact sum of nFeds over all events), so there
  // is no per-event cap and no wasted slots.
  struct ScanResult {
    EventRecord* d_events;  // device array, length = nEvents
    FedEntry* d_feds;       // device array, length = totalFeds
    uint32_t nEvents;
    uint32_t totalFeds;
  };

  ScanResult scanChunkOnDevice(const unsigned char* d_chunk,
                               uint64_t chunkSize,
                               uint64_t firstHeaderOffset,
                               uint32_t maxEvents);

}  // namespace frdscan

#endif
