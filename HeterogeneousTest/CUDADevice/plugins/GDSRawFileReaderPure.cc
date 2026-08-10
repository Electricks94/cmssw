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

class GDSRawFileReaderPure : public edm::RawInputSource {
public:
  explicit GDSRawFileReaderPure(edm::ParameterSet const& pset, edm::InputSourceDescription const& desc);
  ~GDSRawFileReaderPure() override;

  static void fillDescriptions(edm::ConfigurationDescriptions& descriptions);

protected:
  Next checkNext() override;
  void read(edm::EventPrincipal& eventPrincipal) override;

private:
  uint16_t parseFileHeader(int fd);
  void loadFileToDevice();
  void scanAndCopyIndex();
  bool validateAgainstHost() const;  // temp
  static edm::Timestamp nowTimestamp();

  static constexpr uint32_t kMaxEvents = 8192;

  const std::string inputFile_;
  const bool useGDS_;

  size_t fileSize_ = 0;
  uint16_t rawHeaderSize_ = 0;
  uint32_t runNumber_ = 0;
  uint32_t lumiSection_ = 0;

  unsigned char* devPtr_ = nullptr;
  unsigned char* hostMirror_ = nullptr;  // byte source for the host product; removed with the device-resident product

  std::vector<frdscan::EventRecord> events_;  // event table produced by the device scan
  std::vector<frdscan::FedEntry> feds_;       // host copy: legacy product + validation only
  frdscan::FedEntry* d_feds_ = nullptr;       // the SAME index, kept RESIDENT on the device
  size_t nextEvent_ = 0;

  const edm::DaqProvenanceHelper daqProvenanceHelper_{edm::TypeID(typeid(FEDRawDataCollection))};
  const edm::DaqProvenanceHelper deviceProvenanceHelper_{edm::TypeID(typeid(gdsraw::RawDataDeviceRef))};
  edm::ProcessHistoryID processHistoryID_;
};

GDSRawFileReaderPure::GDSRawFileReaderPure(edm::ParameterSet const& pset, edm::InputSourceDescription const& desc)
    : edm::RawInputSource(pset, desc),
      inputFile_(pset.getParameter<std::string>("inputFile")),
      useGDS_(pset.getParameter<bool>("useGDS")) {
  int fd = ::open(inputFile_.c_str(), O_RDONLY);
  if (fd < 0)
    throw cms::Exception("GDSRawFileReaderPure") << "cannot open " << inputFile_;
  struct stat st;
  ::fstat(fd, &st);
  fileSize_ = st.st_size;
  rawHeaderSize_ = parseFileHeader(fd);
  ::close(fd);

  loadFileToDevice();
  scanAndCopyIndex();

  if (events_.empty())
    throw cms::Exception("GDSRawFileReaderPure") << "scan found no events in " << inputFile_;

  validateAgainstHost();

  runNumber_ = events_[0].run;
  lumiSection_ = events_[0].lumi;
  std::cout << "GDSRawFileReaderPure: " << inputFile_ << " size " << fileSize_ << " run " << runNumber_ << " lumi "
            << lumiSection_ << " events " << events_.size() << std::endl;

  processHistoryID_ = daqProvenanceHelper_.daqInit(productRegistryUpdate(), processHistoryRegistryForUpdate());
  deviceProvenanceHelper_.daqInit(productRegistryUpdate(), processHistoryRegistryForUpdate());

  setRunAuxiliary(new edm::RunAuxiliary(runNumber_, nowTimestamp(), edm::Timestamp::invalidTimestamp()));
  setLuminosityBlockAuxiliary(
      new edm::LuminosityBlockAuxiliary(runNumber_, lumiSection_, nowTimestamp(), edm::Timestamp::invalidTimestamp()));
  setNewRun();
  setNewLumi();
  setEventCached();
}

GDSRawFileReaderPure::~GDSRawFileReaderPure() {
  if (d_feds_)
    cudaFree(d_feds_);
  if (devPtr_) {
    if (useGDS_)
      cuFileBufDeregister(devPtr_);
    cudaFree(devPtr_);
  }
  if (hostMirror_)
    cudaFreeHost(hostMirror_);
  if (useGDS_)
    cuFileDriverClose();
}

void GDSRawFileReaderPure::fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
  edm::ParameterSetDescription desc;
  desc.setComment("Input source reading an FRD .raw file into GPU memory via GPUDirect Storage");
  desc.add<std::string>("inputFile");
  desc.add<bool>("useGDS", true);
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

void GDSRawFileReaderPure::loadFileToDevice() {
  const size_t allocSize = (fileSize_ + 4095) & ~size_t(4095);
  cudaCheck(cudaMalloc(&devPtr_, allocSize));
  cudaCheck(cudaMallocHost((void**)&hostMirror_, fileSize_));

  using hrclock = std::chrono::high_resolution_clock;
  auto ms = [](hrclock::time_point a, hrclock::time_point b) {
    return std::chrono::duration<double, std::milli>(b - a).count();
  };

  if (useGDS_) {
    auto t0 = hrclock::now();
    CUfileError_t status = cuFileDriverOpen();
    if (status.err != CU_FILE_SUCCESS)
      throw cms::Exception("GDSRawFileReaderPure") << "cuFileDriverOpen failed: " << status.err;

    int fd = ::open(inputFile_.c_str(), O_RDONLY | O_DIRECT);
    if (fd < 0)
      fd = ::open(inputFile_.c_str(), O_RDONLY);
    if (fd < 0)
      throw cms::Exception("GDSRawFileReaderPure") << "cannot open " << inputFile_;

    CUfileHandle_t cfHandle;
    CUfileDescr_t cfDescr = {};
    cfDescr.handle.fd = fd;
    cfDescr.type = CU_FILE_HANDLE_TYPE_OPAQUE_FD;
    status = cuFileHandleRegister(&cfHandle, &cfDescr);
    if (status.err != CU_FILE_SUCCESS)
      throw cms::Exception("GDSRawFileReaderPure") << "cuFileHandleRegister failed: " << status.err;

    status = cuFileBufRegister(devPtr_, allocSize, 0);
    if (status.err != CU_FILE_SUCCESS)
      throw cms::Exception("GDSRawFileReaderPure") << "cuFileBufRegister failed: " << status.err;

    auto t1 = hrclock::now();
    ssize_t got = cuFileRead(cfHandle, devPtr_, fileSize_, 0, 0);
    auto t2 = hrclock::now();
    if (got < 0 || size_t(got) != fileSize_)
      throw cms::Exception("GDSRawFileReaderPure") << "cuFileRead returned " << got;

    cuFileHandleDeregister(cfHandle);
    ::close(fd);

    cudaCheck(cudaMemcpy(hostMirror_, devPtr_, fileSize_, cudaMemcpyDeviceToHost));
    cudaCheck(cudaDeviceSynchronize());
    auto t3 = hrclock::now();

    std::cout << "GDSRawFileReaderPure: cuFile setup (driver open + registrations) " << ms(t0, t1) << " ms" << std::endl;
    std::cout << "GDSRawFileReaderPure: GDS read " << fileSize_ << " bytes in " << ms(t1, t2) << " ms, "
              << (fileSize_ / 1e9) / (ms(t1, t2) / 1e3) << " GB/s" << std::endl;
    std::cout << "GDSRawFileReaderPure: host mirror D2H " << ms(t2, t3) << " ms" << std::endl;
  } else {
    int fd = ::open(inputFile_.c_str(), O_RDONLY);
    if (fd < 0)
      throw cms::Exception("GDSRawFileReaderPure") << "cannot open " << inputFile_;
    auto t1 = hrclock::now();
    size_t done = 0;
    while (done < fileSize_) {
      ssize_t got = ::pread(fd, hostMirror_ + done, fileSize_ - done, done);
      if (got <= 0)
        throw cms::Exception("GDSRawFileReaderPure") << "pread failed at " << done;
      done += got;
    }
    auto t2 = hrclock::now();
    ::close(fd);
    cudaCheck(cudaMemcpy(devPtr_, hostMirror_, fileSize_, cudaMemcpyHostToDevice));
    cudaCheck(cudaDeviceSynchronize());
    auto t3 = hrclock::now();
    std::cout << "GDSRawFileReaderPure: POSIX read " << fileSize_ << " bytes in " << ms(t1, t2) << " ms, "
              << (fileSize_ / 1e9) / (ms(t1, t2) / 1e3) << " GB/s" << std::endl;
    std::cout << "GDSRawFileReaderPure: H2D copy " << ms(t2, t3) << " ms" << std::endl;
  }
}

void GDSRawFileReaderPure::scanAndCopyIndex() {
  auto start = std::chrono::high_resolution_clock::now();
  frdscan::ScanResult sr = frdscan::scanChunkOnDevice(devPtr_, fileSize_, rawHeaderSize_, kMaxEvents);
  auto end = std::chrono::high_resolution_clock::now();

  events_.resize(sr.nEvents);
  feds_.resize(sr.totalFeds);
  if (sr.nEvents > 0)
    cudaCheck(cudaMemcpy(events_.data(), sr.d_events, sr.nEvents * sizeof(frdscan::EventRecord),
                         cudaMemcpyDeviceToHost));
  if (sr.totalFeds > 0)
    cudaCheck(cudaMemcpy(feds_.data(), sr.d_feds, size_t(sr.totalFeds) * sizeof(frdscan::FedEntry),
                         cudaMemcpyDeviceToHost));
  cudaFree(sr.d_events);
  d_feds_ = sr.d_feds;  // NOT freed: the device product points at this

  std::chrono::duration<double, std::milli> elapsed = end - start;
  std::cout << "GDSRawFileReaderPure: device scan produced " << sr.nEvents << " events, " << sr.totalFeds
            << " FED fragments in " << elapsed.count() << " ms" << std::endl;
}

edm::Timestamp GDSRawFileReaderPure::nowTimestamp() {
  timeval now;
  gettimeofday(&now, nullptr);
  return edm::Timestamp(edm::TimeValue_t(uint64_t(now.tv_sec) << 32 | uint64_t(now.tv_usec)));
}

edm::RawInputSource::Next GDSRawFileReaderPure::checkNext() {
  if (nextEvent_ >= events_.size())
    return Next::kStop;
  setEventCached();
  return Next::kEvent;
}

void GDSRawFileReaderPure::read(edm::EventPrincipal& eventPrincipal) {
  const frdscan::EventRecord& e = events_[nextEvent_];

  auto rawData = std::make_unique<FEDRawDataCollection>();
  for (uint32_t k = 0; k < e.nFeds; ++k) {
    const frdscan::FedEntry& f = feds_[e.fedIndexBase + k];
    FEDRawData& fedData = rawData->FEDData(f.fedId);
    fedData.resize(f.size);
    memcpy(fedData.data(), hostMirror_ + f.offset, f.size);
  }

  const bool isRealData = !(e.flags & 0x1u);  // FRDEVENT_MASK_ISGENDATA, from the scan record

  edm::EventID id(e.run, e.lumi, e.event);
  edm::EventAuxiliary aux(id, processGUID(), nowTimestamp(), isRealData, edm::EventAuxiliary::PhysicsTrigger);
  aux.setProcessHistoryID(processHistoryID_);
  makeEvent(eventPrincipal, aux);

  // the device product: two pointers and a count, no payload bytes
  auto deviceRef = std::make_unique<gdsraw::RawDataDeviceRef>(devPtr_, d_feds_ + e.fedIndexBase, e.nFeds);
  std::unique_ptr<edm::WrapperBase> devp(new edm::Wrapper<gdsraw::RawDataDeviceRef>(std::move(deviceRef)));
  eventPrincipal.put(
      deviceProvenanceHelper_.productDescription(), std::move(devp), deviceProvenanceHelper_.dummyProvenance());

  std::unique_ptr<edm::WrapperBase> edp(new edm::Wrapper<FEDRawDataCollection>(std::move(rawData)));
  eventPrincipal.put(daqProvenanceHelper_.productDescription(), std::move(edp), daqProvenanceHelper_.dummyProvenance());

  ++nextEvent_;
}

bool GDSRawFileReaderPure::validateAgainstHost() const {
  using namespace edm::streamer;
  bool ok = true;
  uint64_t pos = rawHeaderSize_;
  uint32_t n = 0;

  for (; n < events_.size(); ++n) {
    if (pos + FRDHeaderVersionSize[FRDHeaderMaxVersion] > fileSize_)
      break;
    FRDEventMsgView view((void*)(hostMirror_ + pos));
    if (pos + view.size() > fileSize_)
      break;

    const frdscan::EventRecord& g = events_[n];
    if (g.headerOffset != pos || g.payloadSize != view.eventSize() || g.run != view.run() ||
        g.lumi != view.lumi() || g.event != view.event()) {
      std::cout << "validateAgainstHost: event " << n << " field mismatch (run cpu " << view.run() << " gpu " << g.run
                << ", event cpu " << view.event() << " gpu " << g.event << ")" << std::endl;
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
      const FEDHeader header(event + eventSize);
      const uint16_t fedId = header.sourceID();
      bool found = false;
      for (uint32_t k = 0; k < g.nFeds; ++k) {
        const frdscan::FedEntry& f = feds_[g.fedIndexBase + k];
        if (f.fedId == fedId && f.size == fedSize) {
          found = true;
          break;
        }
      }
      if (!found) {
        std::cout << "validateAgainstHost: event " << n << " fedId " << fedId << " size " << fedSize
                  << " missing from scan" << std::endl;
        ok = false;
      }
      ++cpuNFeds;
    }
    if (cpuNFeds != g.nFeds) {
      std::cout << "validateAgainstHost: event " << n << " nFeds cpu " << cpuNFeds << " gpu " << g.nFeds << std::endl;
      ok = false;
    }
    if (g.truncated) {
      std::cout << "validateAgainstHost: event " << n << " scan reported truncated FED walk" << std::endl;
      ok = false;
    }
    pos += view.size();
  }
  if (n != events_.size()) {
    std::cout << "validateAgainstHost: event count cpu " << n << " gpu " << events_.size() << std::endl;
    ok = false;
  }

  std::cout << "validateAgainstHost: " << (ok ? "PASSED" : "FAILED") << " (" << events_.size() << " events)"
            << std::endl;
  return ok;
}

#include "FWCore/Framework/interface/InputSourceMacros.h"
DEFINE_FWK_INPUT_SOURCE(GDSRawFileReaderPure);
