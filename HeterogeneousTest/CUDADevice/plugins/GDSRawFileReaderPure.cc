#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#include <cuda_runtime.h>
#include "cufile.h"

#include "DataFormats/Common/interface/Wrapper.h"
#include "DataFormats/FEDRawData/interface/FEDHeader.h"
#include "DataFormats/FEDRawData/interface/FEDRawData.h"
#include "DataFormats/FEDRawData/interface/FEDRawDataCollection.h"
#include "DataFormats/FEDRawData/interface/FEDTrailer.h"
#include "DataFormats/Provenance/interface/EventAuxiliary.h"
#include "DataFormats/Provenance/interface/EventID.h"
#include "DataFormats/Provenance/interface/LuminosityBlockAuxiliary.h"
#include "DataFormats/Provenance/interface/RunAuxiliary.h"
#include "DataFormats/Provenance/interface/Timestamp.h"
#include "FWCore/Framework/interface/EventPrincipal.h"
#include "FWCore/Framework/interface/InputSourceDescription.h"
#include "FWCore/ParameterSet/interface/ConfigurationDescriptions.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/ParameterSet/interface/ParameterSetDescription.h"
#include "FWCore/Sources/interface/DaqProvenanceHelper.h"
#include "FWCore/Sources/interface/RawInputSource.h"
#include "FWCore/Utilities/interface/Exception.h"
#include "HeterogeneousCore/CUDAUtilities/interface/cudaCheck.h"

#include "IOPool/Streamer/interface/FRDEventMessage.h"
#include "IOPool/Streamer/interface/FRDFileHeader.h"

#include "HeterogeneousTest/CUDADevice/interface/FRDScan.h"
#include "HeterogeneousTest/CUDADevice/interface/GDSRawDataDeviceRef.h"

using namespace cms::cuda;
#define cudaCheck(ARG) cms::cuda::cudaCheck(__FILE__, __LINE__, __func__, (ARG))

// Multi-file GDS input source.
//
// Files are read one at a time into a small RING of device buffers (nSlots),
// mirroring the numBuffers ring in FedRawDataInputSource. The ring matters for
// correctness: events handed to the framework keep pointing into the buffer they
// were parsed from, so a buffer must not be overwritten while its events may
// still be in flight downstream. With nSlots = 3 the buffer for file K is only
// reused at file K+3, by which time file K's events are long retired.

class GDSRawFileReaderPure : public edm::RawInputSource {
public:
  explicit GDSRawFileReaderPure(edm::ParameterSet const& pset, edm::InputSourceDescription const& desc);
  ~GDSRawFileReaderPure() override;

  static void fillDescriptions(edm::ConfigurationDescriptions& descriptions);

protected:
  Next checkNext() override;
  void read(edm::EventPrincipal& eventPrincipal) override;

private:
  struct Slot {
    unsigned char* devPtr = nullptr;             // device chunk (whole file)
    unsigned char* hostMirror = nullptr;         // only when a host copy is needed
    frdscan::FedEntry* d_feds = nullptr;         // device FED index, kept resident
    std::vector<frdscan::EventRecord> events;    // host event table (scheduling)
    std::vector<frdscan::FedEntry> feds;         // host FED index (legacy product only)
    size_t fileSize = 0;
    std::string fileName;
  };

  static uint16_t parseFileHeader(int fd);
  static edm::Timestamp nowTimestamp();
  void loadFile(size_t fileIndex);
  void maybeOpenNewLumi(uint32_t lumi);
  bool validateSlot(Slot const& s, uint16_t rawHeaderSize) const;

  static constexpr uint32_t kMaxEventsPerFile = 8192;

  std::vector<std::string> inputFiles_;
  const bool useGDS_;
  const bool produceLegacy_;
  const bool validate_;
  const bool verbose_;
  const uint32_t nSlots_;

  size_t allocSize_ = 0;   // every slot is sized to the largest file
  bool needHost_ = false;  // whether a host mirror is required at all

  std::vector<Slot> slots_;
  size_t nextFile_ = 0;     // index of the next file to load
  size_t currentSlot_ = 0;  // slot holding the file being served
  size_t nextEvent_ = 0;    // event index within that slot

  uint32_t runNumber_ = 0;

  // running totals for the end-of-job summary
  double totalReadMs_ = 0.0;
  double totalScanMs_ = 0.0;
  size_t totalBytes_ = 0;
  size_t totalEvents_ = 0;
  size_t totalFragments_ = 0;
  size_t filesValidated_ = 0;
  size_t filesFailed_ = 0;

  const edm::DaqProvenanceHelper daqProvenanceHelper_{edm::TypeID(typeid(FEDRawDataCollection))};
  const edm::DaqProvenanceHelper deviceProvenanceHelper_{edm::TypeID(typeid(gdsraw::RawDataDeviceRef))};
  edm::ProcessHistoryID processHistoryID_;
};

GDSRawFileReaderPure::GDSRawFileReaderPure(edm::ParameterSet const& pset, edm::InputSourceDescription const& desc)
    : edm::RawInputSource(pset, desc),
      useGDS_(pset.getParameter<bool>("useGDS")),
      produceLegacy_(pset.getParameter<bool>("produceLegacy")),
      validate_(pset.getParameter<bool>("validate")),
      verbose_(pset.getParameter<bool>("verbose")),
      nSlots_(pset.getParameter<uint32_t>("nSlots")) {
  // accept either a single file or a list, so older configs keep working
  inputFiles_ = pset.getParameter<std::vector<std::string>>("inputFiles");
  const std::string one = pset.getParameter<std::string>("inputFile");
  if (not one.empty())
    inputFiles_.insert(inputFiles_.begin(), one);
  if (inputFiles_.empty())
    throw cms::Exception("GDSRawFileReaderPure") << "no input files given (set inputFile or inputFiles)";
  if (nSlots_ < 1)
    throw cms::Exception("GDSRawFileReaderPure") << "nSlots must be >= 1";

  needHost_ = produceLegacy_ or validate_ or not useGDS_;

  // size every slot to the largest file so buffers can be reused and registered once
  size_t maxFileSize = 0;
  for (auto const& f : inputFiles_) {
    struct stat st;
    if (::stat(f.c_str(), &st) != 0)
      throw cms::Exception("GDSRawFileReaderPure") << "cannot stat " << f;
    maxFileSize = std::max(maxFileSize, size_t(st.st_size));
  }
  allocSize_ = (maxFileSize + 4095) & ~size_t(4095);

  std::cout << "GDSRawFileReaderPure: " << inputFiles_.size() << " input files, largest " << maxFileSize << " bytes, "
            << nSlots_ << " device slots of " << allocSize_ << " bytes ("
            << (double(nSlots_) * allocSize_ / (1 << 20)) << " MiB device)" << std::endl;

  if (useGDS_) {
    CUfileError_t status = cuFileDriverOpen();
    if (status.err != CU_FILE_SUCCESS)
      throw cms::Exception("GDSRawFileReaderPure") << "cuFileDriverOpen failed: " << status.err;
  }

  slots_.resize(nSlots_);
  for (auto& s : slots_) {
    cudaCheck(cudaMalloc(&s.devPtr, allocSize_));
    if (needHost_)
      cudaCheck(cudaMallocHost((void**)&s.hostMirror, allocSize_));
    if (useGDS_) {
      // register once per slot; the expensive BAR1 mapping is amortised over all files
      CUfileError_t status = cuFileBufRegister(s.devPtr, allocSize_, 0);
      if (status.err != CU_FILE_SUCCESS)
        throw cms::Exception("GDSRawFileReaderPure") << "cuFileBufRegister failed: " << status.err;
    }
  }

  // load the first file so an event is ready before the framework asks
  loadFile(nextFile_++);
  if (slots_[currentSlot_].events.empty())
    throw cms::Exception("GDSRawFileReaderPure") << "no events found in " << slots_[currentSlot_].fileName;

  runNumber_ = slots_[currentSlot_].events[0].run;

  processHistoryID_ = daqProvenanceHelper_.daqInit(productRegistryUpdate(), processHistoryRegistryForUpdate());
  deviceProvenanceHelper_.daqInit(productRegistryUpdate(), processHistoryRegistryForUpdate());

  setRunAuxiliary(new edm::RunAuxiliary(runNumber_, nowTimestamp(), edm::Timestamp::invalidTimestamp()));
  setNewRun();
  maybeOpenNewLumi(slots_[currentSlot_].events[0].lumi);
  setEventCached();
}

GDSRawFileReaderPure::~GDSRawFileReaderPure() {
  const double gbps = totalReadMs_ > 0 ? (totalBytes_ / 1e9) / (totalReadMs_ / 1e3) : 0.0;
  std::cout << "GDSRawFileReaderPure SUMMARY: files " << nextFile_ << "  bytes " << totalBytes_ << "  events "
            << totalEvents_ << "  fragments " << totalFragments_ << "\n"
            << "  " << (useGDS_ ? "GDS read" : "POSIX read + H2D") << " " << totalReadMs_ << " ms (" << gbps
            << " GB/s aggregate)   device scan " << totalScanMs_ << " ms" << std::endl;
  if (validate_)
    std::cout << "  validation: " << filesValidated_ << " files passed, " << filesFailed_ << " failed" << std::endl;

  for (auto& s : slots_) {
    if (s.d_feds)
      cudaFree(s.d_feds);
    if (s.devPtr) {
      if (useGDS_)
        cuFileBufDeregister(s.devPtr);
      cudaFree(s.devPtr);
    }
    if (s.hostMirror)
      cudaFreeHost(s.hostMirror);
  }
  if (useGDS_)
    cuFileDriverClose();
}

void GDSRawFileReaderPure::fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
  edm::ParameterSetDescription desc;
  desc.setComment("Input source reading FRD .raw files into GPU memory via GPUDirect Storage");
  desc.add<std::string>("inputFile", "");                            // single file (legacy configs)
  desc.add<std::vector<std::string>>("inputFiles", {});              // list of files
  desc.add<bool>("useGDS", true);
  desc.add<bool>("produceLegacy", false);  // also build the host FEDRawDataCollection
  desc.add<bool>("validate", false);       // byte-level cross check of the scan, per file
  desc.add<bool>("verbose", true);         // one line per file
  desc.add<uint32_t>("nSlots", 3);         // device buffer ring depth
  edm::RawInputSource::fillDescription(desc);
  descriptions.add("source", desc);
}

uint16_t GDSRawFileReaderPure::parseFileHeader(int fd) {
  using namespace edm::streamer;
  unsigned char buf[sizeof(FRDFileHeader_v2)];
  if (::pread(fd, buf, sizeof(buf), 0) != (ssize_t)sizeof(buf))
    return 0;
  auto const* id = reinterpret_cast<FRDFileHeaderIdentifier const*>(buf);
  if (std::memcmp(id->id_.data(), FRDFileHeader_id.data(), 4) != 0)
    return 0;
  if (std::memcmp(id->version_.data(), FRDFileVersion_2.data(), 4) == 0)
    return reinterpret_cast<FRDFileHeader_v2 const*>(buf)->c_.headerSize_;
  if (std::memcmp(id->version_.data(), FRDFileVersion_1.data(), 4) == 0)
    return reinterpret_cast<FRDFileHeader_v1 const*>(buf)->c_.headerSize_;
  return 0;
}

edm::Timestamp GDSRawFileReaderPure::nowTimestamp() {
  timeval now;
  gettimeofday(&now, nullptr);
  return edm::Timestamp(edm::TimeValue_t(uint64_t(now.tv_sec) << 32 | uint64_t(now.tv_usec)));
}

void GDSRawFileReaderPure::loadFile(size_t fileIndex) {
  using hrclock = std::chrono::high_resolution_clock;
  auto ms = [](hrclock::time_point a, hrclock::time_point b) {
    return std::chrono::duration<double, std::milli>(b - a).count();
  };

  Slot& s = slots_[fileIndex % nSlots_];
  s.fileName = inputFiles_[fileIndex];

  int fd = ::open(s.fileName.c_str(), O_RDONLY);
  if (fd < 0)
    throw cms::Exception("GDSRawFileReaderPure") << "cannot open " << s.fileName;
  struct stat st;
  ::fstat(fd, &st);
  s.fileSize = st.st_size;
  const uint16_t rawHeaderSize = parseFileHeader(fd);
  ::close(fd);

  if (s.fileSize > allocSize_)
    throw cms::Exception("GDSRawFileReaderPure") << s.fileName << " is larger than the allocated slot";

  // ---------------- read the file into the slot's device buffer ----------------
  auto t0 = hrclock::now();
  if (useGDS_) {
    int rfd = ::open(s.fileName.c_str(), O_RDONLY | O_DIRECT);
    if (rfd < 0)
      rfd = ::open(s.fileName.c_str(), O_RDONLY);
    if (rfd < 0)
      throw cms::Exception("GDSRawFileReaderPure") << "cannot open " << s.fileName;

    CUfileHandle_t cfHandle;
    CUfileDescr_t cfDescr = {};
    cfDescr.handle.fd = rfd;
    cfDescr.type = CU_FILE_HANDLE_TYPE_OPAQUE_FD;
    CUfileError_t status = cuFileHandleRegister(&cfHandle, &cfDescr);
    if (status.err != CU_FILE_SUCCESS)
      throw cms::Exception("GDSRawFileReaderPure") << "cuFileHandleRegister failed: " << status.err;

    ssize_t got = cuFileRead(cfHandle, s.devPtr, s.fileSize, 0, 0);
    cuFileHandleDeregister(cfHandle);
    ::close(rfd);
    if (got < 0 || size_t(got) != s.fileSize)
      throw cms::Exception("GDSRawFileReaderPure") << "cuFileRead returned " << got << " for " << s.fileName;

    if (needHost_)
      cudaCheck(cudaMemcpy(s.hostMirror, s.devPtr, s.fileSize, cudaMemcpyDeviceToHost));
  } else {
    int rfd = ::open(s.fileName.c_str(), O_RDONLY);
    if (rfd < 0)
      throw cms::Exception("GDSRawFileReaderPure") << "cannot open " << s.fileName;
    size_t done = 0;
    while (done < s.fileSize) {
      ssize_t got = ::pread(rfd, s.hostMirror + done, s.fileSize - done, done);
      if (got <= 0)
        throw cms::Exception("GDSRawFileReaderPure") << "pread failed at " << done << " in " << s.fileName;
      done += got;
    }
    ::close(rfd);
    cudaCheck(cudaMemcpy(s.devPtr, s.hostMirror, s.fileSize, cudaMemcpyHostToDevice));
  }
  cudaCheck(cudaDeviceSynchronize());
  auto t1 = hrclock::now();

  // ---------------- parse it on the device ----------------
  if (s.d_feds) {
    cudaFree(s.d_feds);
    s.d_feds = nullptr;
  }
  frdscan::ScanResult sr = frdscan::scanChunkOnDevice(s.devPtr, s.fileSize, rawHeaderSize, kMaxEventsPerFile);
  s.events.resize(sr.nEvents);
  if (sr.nEvents > 0)
    cudaCheck(cudaMemcpy(s.events.data(), sr.d_events, sr.nEvents * sizeof(frdscan::EventRecord),
                         cudaMemcpyDeviceToHost));
  s.d_feds = sr.d_feds;  // stays on the device: the product points at it
  if (produceLegacy_ or validate_) {
    s.feds.resize(sr.totalFeds);
    if (sr.totalFeds > 0)
      cudaCheck(cudaMemcpy(s.feds.data(), sr.d_feds, size_t(sr.totalFeds) * sizeof(frdscan::FedEntry),
                           cudaMemcpyDeviceToHost));
  }
  cudaFree(sr.d_events);
  auto t2 = hrclock::now();

  totalReadMs_ += ms(t0, t1);
  totalScanMs_ += ms(t1, t2);
  totalBytes_ += s.fileSize;
  totalFragments_ += sr.totalFeds;

  if (validate_) {
    if (validateSlot(s, rawHeaderSize))
      ++filesValidated_;
    else
      ++filesFailed_;
  }

  if (verbose_) {
    const double gbps = (s.fileSize / 1e9) / (ms(t0, t1) / 1e3);
    std::cout << "GDSRawFileReaderPure: [" << (fileIndex + 1) << "/" << inputFiles_.size() << "] "
              << s.fileName.substr(s.fileName.find_last_of('/') + 1) << "  " << s.fileSize << " B  read "
              << ms(t0, t1) << " ms (" << gbps << " GB/s)  scan " << ms(t1, t2) << " ms  events "
              << sr.nEvents << "  fragments " << sr.totalFeds << std::endl;
  }

  currentSlot_ = fileIndex % nSlots_;
  nextEvent_ = 0;
}

void GDSRawFileReaderPure::maybeOpenNewLumi(uint32_t lumi) {
  if (not luminosityBlockAuxiliary() or luminosityBlockAuxiliary()->luminosityBlock() != lumi) {
    auto* lba =
        new edm::LuminosityBlockAuxiliary(runNumber_, lumi, nowTimestamp(), edm::Timestamp::invalidTimestamp());
    lba->setProcessHistoryID(processHistoryID_);
    setLuminosityBlockAuxiliary(lba);
    setNewLumi();
  }
}

edm::RawInputSource::Next GDSRawFileReaderPure::checkNext() {
  // move on to the next file when the current one is drained
  while (nextEvent_ >= slots_[currentSlot_].events.size()) {
    if (nextFile_ >= inputFiles_.size())
      return Next::kStop;
    loadFile(nextFile_++);
  }
  // files may span lumisections (e.g. ls0214 and ls0218 in run 402360)
  maybeOpenNewLumi(slots_[currentSlot_].events[nextEvent_].lumi);
  setEventCached();
  return Next::kEvent;
}

void GDSRawFileReaderPure::read(edm::EventPrincipal& eventPrincipal) {
  Slot& s = slots_[currentSlot_];
  const frdscan::EventRecord& e = s.events[nextEvent_];

  const bool isRealData = !(e.flags & 0x1u);  // FRDEVENT_MASK_ISGENDATA
  edm::EventID id(e.run, e.lumi, e.event);
  edm::EventAuxiliary aux(id, processGUID(), nowTimestamp(), isRealData, edm::EventAuxiliary::PhysicsTrigger);
  aux.setProcessHistoryID(processHistoryID_);
  makeEvent(eventPrincipal, aux);

  // device product: two pointers and a count, no payload bytes
  auto deviceRef = std::make_unique<gdsraw::RawDataDeviceRef>(s.devPtr, s.d_feds + e.fedIndexBase, e.nFeds);
  std::unique_ptr<edm::WrapperBase> devp(new edm::Wrapper<gdsraw::RawDataDeviceRef>(std::move(deviceRef)));
  eventPrincipal.put(
      deviceProvenanceHelper_.productDescription(), std::move(devp), deviceProvenanceHelper_.dummyProvenance());

  if (produceLegacy_) {
    auto rawData = std::make_unique<FEDRawDataCollection>();
    for (uint32_t k = 0; k < e.nFeds; ++k) {
      const frdscan::FedEntry& f = s.feds[e.fedIndexBase + k];
      FEDRawData& fedData = rawData->FEDData(f.fedId);
      fedData.resize(f.size);
      memcpy(fedData.data(), s.hostMirror + f.offset, f.size);
    }
    std::unique_ptr<edm::WrapperBase> edp(new edm::Wrapper<FEDRawDataCollection>(std::move(rawData)));
    eventPrincipal.put(
        daqProvenanceHelper_.productDescription(), std::move(edp), daqProvenanceHelper_.dummyProvenance());
  }

  ++nextEvent_;
  ++totalEvents_;
}

bool GDSRawFileReaderPure::validateSlot(Slot const& s, uint16_t rawHeaderSize) const {
  using namespace edm::streamer;
  bool ok = true;
  uint64_t pos = rawHeaderSize;
  uint32_t n = 0;

  for (; n < s.events.size(); ++n) {
    if (pos + FRDHeaderVersionSize[FRDHeaderMaxVersion] > s.fileSize)
      break;
    FRDEventMsgView view((void*)(s.hostMirror + pos));
    if (pos + view.size() > s.fileSize)
      break;

    const frdscan::EventRecord& g = s.events[n];
    if (g.headerOffset != pos || g.payloadSize != view.eventSize() || g.run != view.run() ||
        g.lumi != view.lumi() || g.event != view.event()) {
      std::cout << "validate: " << s.fileName << " event " << n << " field mismatch" << std::endl;
      ok = false;
    }

    uint32_t eventSize = view.eventSize();
    unsigned char* event = (unsigned char*)view.payload();
    uint32_t cpuNFeds = 0;
    while (eventSize > 0) {
      eventSize -= FEDTrailer::length;
      const FEDTrailer trailer(event + eventSize);
      const uint32_t fedSize = trailer.fragmentLength() * sizeof(uint64_t);
      eventSize -= (fedSize - FEDTrailer::length);
      ++cpuNFeds;
    }
    if (cpuNFeds != g.nFeds || g.truncated) {
      std::cout << "validate: " << s.fileName << " event " << n << " nFeds cpu " << cpuNFeds << " gpu " << g.nFeds
                << (g.truncated ? " (truncated)" : "") << std::endl;
      ok = false;
    }
    pos += view.size();
  }
  if (n != s.events.size()) {
    std::cout << "validate: " << s.fileName << " event count cpu " << n << " gpu " << s.events.size() << std::endl;
    ok = false;
  }
  return ok;
}

#include "FWCore/Framework/interface/InputSourceMacros.h"
DEFINE_FWK_INPUT_SOURCE(GDSRawFileReaderPure);
