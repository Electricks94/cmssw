#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>

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


using namespace cms::cuda;
#define cudaCheck(ARG) cms::cuda::cudaCheck(__FILE__, __LINE__, __func__, (ARG))

class GDSRawFileReaderNew : public edm::RawInputSource {
public:
  explicit GDSRawFileReaderNew(edm::ParameterSet const& pset, edm::InputSourceDescription const& desc);
  ~GDSRawFileReaderNew() override;

  static void fillDescriptions(edm::ConfigurationDescriptions& descriptions);

protected:
  Next checkNext() override;
  void read(edm::EventPrincipal& eventPrincipal) override;

private:
  uint16_t parseFileHeader(int fd);
  void loadFileToDevice();
  static edm::Timestamp nowTimestamp();

  const std::string inputFile_;
  const bool useGDS_;

  size_t fileSize_ = 0;
  uint16_t rawHeaderSize_ = 0;
  uint32_t runNumber_ = 0;
  uint32_t lumiSection_ = 0;

  unsigned char* devPtr_ = nullptr;
  unsigned char* hostMirror_ = nullptr;  // scaffolding until a device scan kernel exists
  size_t position_ = 0;

  const edm::DaqProvenanceHelper daqProvenanceHelper_{edm::TypeID(typeid(FEDRawDataCollection))};
  edm::ProcessHistoryID processHistoryID_;
};

GDSRawFileReaderNew::GDSRawFileReaderNew(edm::ParameterSet const& pset, edm::InputSourceDescription const& desc)
    : edm::RawInputSource(pset, desc),
      inputFile_(pset.getParameter<std::string>("inputFile")),
      useGDS_(pset.getParameter<bool>("useGDS")) {
  int fd = ::open(inputFile_.c_str(), O_RDONLY);
  if (fd < 0)
    throw cms::Exception("GDSRawFileReaderNew") << "cannot open " << inputFile_;
  struct stat st;
  ::fstat(fd, &st);
  fileSize_ = st.st_size;
  rawHeaderSize_ = parseFileHeader(fd);
  ::close(fd);

  loadFileToDevice();

  position_ = rawHeaderSize_;
  edm::streamer::FRDEventMsgView first((void*)(hostMirror_ + position_));
  runNumber_ = first.run();
  lumiSection_ = first.lumi();
  std::cout << "GDSRawFileReaderNew: " << inputFile_ << " size " << fileSize_ << " run " << runNumber_ << " lumi "
            << lumiSection_ << std::endl;

  processHistoryID_ = daqProvenanceHelper_.daqInit(productRegistryUpdate(), processHistoryRegistryForUpdate());

  setRunAuxiliary(new edm::RunAuxiliary(runNumber_, nowTimestamp(), edm::Timestamp::invalidTimestamp()));
  setLuminosityBlockAuxiliary(
      new edm::LuminosityBlockAuxiliary(runNumber_, lumiSection_, nowTimestamp(), edm::Timestamp::invalidTimestamp()));
  setNewRun();
  setNewLumi();
  setEventCached();
}

GDSRawFileReaderNew::~GDSRawFileReaderNew() {
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

void GDSRawFileReaderNew::fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
  edm::ParameterSetDescription desc;
  desc.setComment("Input source reading an FRD .raw file into GPU memory via GPUDirect Storage");
  desc.add<std::string>("inputFile");
  desc.add<bool>("useGDS", true);
  edm::RawInputSource::fillDescription(desc);
  descriptions.add("source", desc);
}

uint16_t GDSRawFileReaderNew::parseFileHeader(int fd) {
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

void GDSRawFileReaderNew::loadFileToDevice() {
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
      throw cms::Exception("GDSRawFileReaderNew") << "cuFileDriverOpen failed: " << status.err;

    int fd = ::open(inputFile_.c_str(), O_RDONLY | O_DIRECT);
    if (fd < 0)
      fd = ::open(inputFile_.c_str(), O_RDONLY);
    if (fd < 0)
      throw cms::Exception("GDSRawFileReaderNew") << "cannot open " << inputFile_;

    CUfileHandle_t cfHandle;
    CUfileDescr_t cfDescr = {};
    cfDescr.handle.fd = fd;
    cfDescr.type = CU_FILE_HANDLE_TYPE_OPAQUE_FD;
    status = cuFileHandleRegister(&cfHandle, &cfDescr);
    if (status.err != CU_FILE_SUCCESS)
      throw cms::Exception("GDSRawFileReaderNew") << "cuFileHandleRegister failed: " << status.err;

    status = cuFileBufRegister(devPtr_, allocSize, 0);
    if (status.err != CU_FILE_SUCCESS)
      throw cms::Exception("GDSRawFileReaderNew") << "cuFileBufRegister failed: " << status.err;

    auto t1 = hrclock::now();
    ssize_t got = cuFileRead(cfHandle, devPtr_, fileSize_, 0, 0);
    auto t2 = hrclock::now();
    if (got < 0 || size_t(got) != fileSize_)
      throw cms::Exception("GDSRawFileReaderNew") << "cuFileRead returned " << got;

    cuFileHandleDeregister(cfHandle);
    ::close(fd);

    cudaCheck(cudaMemcpy(hostMirror_, devPtr_, fileSize_, cudaMemcpyDeviceToHost));
    cudaCheck(cudaDeviceSynchronize());
    auto t3 = hrclock::now();

    std::cout << "GDSRawFileReaderNew: cuFile setup (driver open + registrations) " << ms(t0, t1) << " ms" << std::endl;
    std::cout << "GDSRawFileReaderNew: GDS read " << fileSize_ << " bytes in " << ms(t1, t2) << " ms, "
              << (fileSize_ / 1e9) / (ms(t1, t2) / 1e3) << " GB/s" << std::endl;
    std::cout << "GDSRawFileReaderNew: host mirror D2H " << ms(t2, t3) << " ms" << std::endl;
  } else {
    int fd = ::open(inputFile_.c_str(), O_RDONLY);
    if (fd < 0)
      throw cms::Exception("GDSRawFileReaderNew") << "cannot open " << inputFile_;
    auto t1 = hrclock::now();
    size_t done = 0;
    while (done < fileSize_) {
      ssize_t got = ::pread(fd, hostMirror_ + done, fileSize_ - done, done);
      if (got <= 0)
        throw cms::Exception("GDSRawFileReaderNew") << "pread failed at " << done;
      done += got;
    }
    auto t2 = hrclock::now();
    ::close(fd);
    cudaCheck(cudaMemcpy(devPtr_, hostMirror_, fileSize_, cudaMemcpyHostToDevice));
    cudaCheck(cudaDeviceSynchronize());
    auto t3 = hrclock::now();
    std::cout << "GDSRawFileReaderNew: POSIX read " << fileSize_ << " bytes in " << ms(t1, t2) << " ms, "
              << (fileSize_ / 1e9) / (ms(t1, t2) / 1e3) << " GB/s" << std::endl;
    std::cout << "GDSRawFileReaderNew: H2D copy " << ms(t2, t3) << " ms" << std::endl;
  }
}

edm::Timestamp GDSRawFileReaderNew::nowTimestamp() {
  timeval now;
  gettimeofday(&now, nullptr);
  return edm::Timestamp(edm::TimeValue_t(uint64_t(now.tv_sec) << 32 | uint64_t(now.tv_usec)));
}

edm::RawInputSource::Next GDSRawFileReaderNew::checkNext() {
  using namespace edm::streamer;
  if (position_ + FRDHeaderVersionSize[FRDHeaderMaxVersion] > fileSize_)
    return Next::kStop;
  FRDEventMsgView view((void*)(hostMirror_ + position_));
  if (position_ + view.size() > fileSize_)
    return Next::kStop;
  setEventCached();
  return Next::kEvent;
}

void GDSRawFileReaderNew::read(edm::EventPrincipal& eventPrincipal) {
  using namespace edm::streamer;
  FRDEventMsgView view((void*)(hostMirror_ + position_));

  auto rawData = std::make_unique<FEDRawDataCollection>();
  uint32_t eventSize = view.eventSize();
  unsigned char* event = (unsigned char*)view.payload();
  while (eventSize > 0) {
    eventSize -= FEDTrailer::length;
    const FEDTrailer fedTrailer(event + eventSize);
    const uint32_t fedSize = fedTrailer.fragmentLength() * sizeof(uint64_t);
    eventSize -= (fedSize - FEDTrailer::length);
    const FEDHeader fedHeader(event + eventSize);
    const uint16_t fedId = fedHeader.sourceID();
    FEDRawData& fedData = rawData->FEDData(fedId);
    fedData.resize(fedSize);
    memcpy(fedData.data(), event + eventSize, fedSize);
  }

  edm::EventID id(runNumber_, lumiSection_, view.event());
  edm::EventAuxiliary aux(id, processGUID(), nowTimestamp(), view.isRealData(), edm::EventAuxiliary::PhysicsTrigger);
  aux.setProcessHistoryID(processHistoryID_);
  makeEvent(eventPrincipal, aux);

  std::unique_ptr<edm::WrapperBase> edp(new edm::Wrapper<FEDRawDataCollection>(std::move(rawData)));
  eventPrincipal.put(daqProvenanceHelper_.productDescription(), std::move(edp), daqProvenanceHelper_.dummyProvenance());

  position_ += view.size();
}

#include "FWCore/Framework/interface/InputSourceMacros.h"
DEFINE_FWK_INPUT_SOURCE(GDSRawFileReaderNew);
