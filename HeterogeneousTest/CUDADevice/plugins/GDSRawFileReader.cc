
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cuda_runtime.h>
#include "cufile.h"

#include "FWCore/Framework/interface/Event.h"
#include "FWCore/Framework/interface/Frameworkfwd.h"
#include "FWCore/Framework/interface/global/EDAnalyzer.h"
#include "FWCore/ParameterSet/interface/ConfigurationDescriptions.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/ParameterSet/interface/ParameterSetDescription.h"
#include "FWCore/ServiceRegistry/interface/Service.h"
#include "HeterogeneousCore/CUDAServices/interface/CUDAInterface.h"
#include "HeterogeneousCore/CUDAUtilities/interface/cudaCheck.h"

// the real FRD format definitions used by FedRawDataInputSource
#include "IOPool/Streamer/interface/FRDEventMessage.h"
#include "IOPool/Streamer/interface/FRDFileHeader.h"

using namespace cms::cuda;
#define cudaCheck(ARG) cms::cuda::cudaCheck(__FILE__, __LINE__, __func__, (ARG))

class GDSRawFileReader : public edm::global::EDAnalyzer<> {
public:
  explicit GDSRawFileReader(edm::ParameterSet const& config);
  ~GDSRawFileReader() override = default;

  static void fillDescriptions(edm::ConfigurationDescriptions& descriptions);

  void analyze(edm::StreamID, edm::Event const& event, edm::EventSetup const& setup) const override;

private:
  static uint16_t parseFileHeader(int fd, uint32_t& runNumber, uint32_t& lumi, uint32_t& eventCount);
  static void printFirstEvent(const unsigned char* data, uint16_t rawHeaderSize);

  const std::string inputFile_;
  const uint32_t chunkSizeMB_;
  const bool useGDS_;
  const bool validate_;
};

GDSRawFileReader::GDSRawFileReader(edm::ParameterSet const& config)
    : inputFile_(config.getParameter<std::string>("inputFile")),
      chunkSizeMB_(config.getParameter<uint32_t>("chunkSizeMB")),
      useGDS_(config.getParameter<bool>("useGDS")),
      validate_(config.getParameter<bool>("validate")) {}

void GDSRawFileReader::fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
  edm::ParameterSetDescription desc;
  desc.add<std::string>("inputFile");
  desc.add<uint32_t>("chunkSizeMB", 200);  //default 200; like the run204...py
  desc.add<bool>("useGDS", true);
  desc.add<bool>("validate", false);  //note: when this is enabled the timing and throughput numebrs are not accurate
  descriptions.addWithDefaultLabel(desc);
}

uint16_t GDSRawFileReader::parseFileHeader(int fd, uint32_t& runNumber, uint32_t& lumi, uint32_t& eventCount) {
  using namespace edm::streamer;
  unsigned char buf[sizeof(FRDFileHeader_v2)];
  if (::pread(fd, buf, sizeof(buf), 0) != (ssize_t)sizeof(buf))
    return 0;
  auto const* id = reinterpret_cast<FRDFileHeaderIdentifier const*>(buf);
  if (std::memcmp(id->id_.data(), FRDFileHeader_id.data(), 4) != 0)
    return 0;  // no file header (old format): events start at offset 0
  if (std::memcmp(id->version_.data(), FRDFileVersion_2.data(), 4) == 0) {
    auto const* h = reinterpret_cast<FRDFileHeader_v2 const*>(buf);
    runNumber = h->c_.runNumber_;
    lumi = h->c_.lumiSection_;
    eventCount = h->c_.eventCount_;
    return h->c_.headerSize_;
  }
  if (std::memcmp(id->version_.data(), FRDFileVersion_1.data(), 4) == 0) {
    auto const* h = reinterpret_cast<FRDFileHeader_v1 const*>(buf);
    runNumber = 0;
    lumi = h->c_.lumiSection_;
    eventCount = h->c_.eventCount_;
    return h->c_.headerSize_;
  }
  return 0;
}

void GDSRawFileReader::printFirstEvent(const unsigned char* data, uint16_t rawHeaderSize) {
  using namespace edm::streamer;
  // same overlay trick as getNextEvent(): the bytes ARE the header
  FRDEventMsgView view((void*)(data + rawHeaderSize));
  std::cout << "GDSRawFileReader: first event -: version " << view.version() << "  run " << view.run() << "  lumi "
            << view.lumi() << "  event " << view.event() << "  payload " << view.eventSize() << " bytes" << std::endl;
}

void GDSRawFileReader::analyze(edm::StreamID, edm::Event const&, edm::EventSetup const&) const {
  edm::Service<CUDAInterface> cuda;
  if (not cuda or not cuda->enabled()) {
    std::cout << "The CUDAService is not available or disabled, the test will be skipped." << std::endl;
    return;
  }

  // host side file discovery (stays on host even with GDS)
  int hdrFd = ::open(inputFile_.c_str(), O_RDONLY);
  if (hdrFd < 0) {
    std::cout << "GDSRawFileReader: cannot open " << inputFile_ << std::endl;
    return;
  }
  struct stat st;
  ::fstat(hdrFd, &st);
  const size_t fileSize = st.st_size;

  uint32_t runNumber = 0, lumi = 0, eventCount = 0;
  const uint16_t rawHeaderSize = parseFileHeader(hdrFd, runNumber, lumi, eventCount);
  ::close(hdrFd);

  std::cout << "GDSRawFileReader: " << inputFile_ << "\n  size " << fileSize << " bytes, file header "
            << rawHeaderSize << " bytes, run " << runNumber << ", lumi " << lumi << ", " << eventCount << " events"
            << std::endl;

  const size_t chunkSize = size_t(chunkSizeMB_) << 20;
  const size_t numChunks = (fileSize + chunkSize - 1) / chunkSize;

  unsigned char* devPtr = nullptr;
  cudaCheck(cudaMalloc(&devPtr, chunkSize));

  // small host buffer, only used to peek at the first event header
  unsigned char* hostPeek = nullptr;
  const size_t peekSize = 4096;
  cudaCheck(cudaMallocHost((void**)&hostPeek, peekSize));

  if (useGDS_) {
    CUfileError_t status = cuFileDriverOpen();
    if (status.err != CU_FILE_SUCCESS) {
      std::cout << "cuFileDriverOpen failed: " << status.err << std::endl;
      return;
    }

    int fd = ::open(inputFile_.c_str(), O_RDONLY | O_DIRECT);
    if (fd < 0) {
      std::cout << "GDSRawFileReader: O_DIRECT open failed, retrying without (compat mode likely)" << std::endl;
      fd = ::open(inputFile_.c_str(), O_RDONLY);
      if (fd < 0)
        return;
    }

    CUfileHandle_t cfHandle;
    CUfileDescr_t cfDescr = {};
    cfDescr.handle.fd = fd;
    cfDescr.type = CU_FILE_HANDLE_TYPE_OPAQUE_FD;
    status = cuFileHandleRegister(&cfHandle, &cfDescr);
    if (status.err != CU_FILE_SUCCESS) {
      std::cout << "cuFileHandleRegister failed: " << status.err << std::endl;
      ::close(fd);
      return;
    }

    status = cuFileBufRegister(devPtr, chunkSize, 0);
    if (status.err != CU_FILE_SUCCESS) {
      std::cout << "cuFileBufRegister failed: " << status.err << std::endl;
      cuFileHandleDeregister(cfHandle);
      ::close(fd);
      return;
    }

    // validation resources
    int valFd = -1;
    unsigned char* valDisk = nullptr;
    unsigned char* valDev = nullptr;
    if (validate_) {
      valFd = ::open(inputFile_.c_str(), O_RDONLY);
      cudaCheck(cudaMallocHost((void**)&valDisk, chunkSize));
      cudaCheck(cudaMallocHost((void**)&valDev, chunkSize));
      std::cout << "GDSRawFileReader: validation enabled, throughput numbers below are NOT representative"
                << std::endl;
    }

    size_t totalBytes = 0;
    bool allValid = true;
    auto start = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < numChunks; ++i) {
      const off_t offset = off_t(i) * off_t(chunkSize);
      const size_t toRead = std::min(chunkSize, fileSize - size_t(offset));
      // the readWorker ::read analogue: positional, base pointer + offset 0
      ssize_t got = cuFileRead(cfHandle, devPtr, toRead, offset, 0);
      if (got < 0 || size_t(got) != toRead) {
        std::cout << "cuFileRead failed on chunk " << i << " (returned " << got << ")" << std::endl;
        break;
      }
      totalBytes += got;
      if (i == 0) {
        // prove the bytes landed: pull back the first 4 KiB and parse the
        // first FRD event header out of the device resident data
        cudaCheck(cudaMemcpy(hostPeek, devPtr, peekSize, cudaMemcpyDeviceToHost));
        printFirstEvent(hostPeek, rawHeaderSize);
        std::cout << "print first event ^" << std::endl;
      }
      if (validate_) {
        // byte exact check: device chunk vs the same range read via pread
        size_t done = 0;
        while (done < toRead) {
          ssize_t r = ::pread(valFd, valDisk + done, toRead - done, offset + done);
          if (r <= 0)
            break;
          done += r;
        }
        cudaCheck(cudaMemcpy(valDev, devPtr, toRead, cudaMemcpyDeviceToHost));
        const bool ok = (done == toRead) && (std::memcmp(valDisk, valDev, toRead) == 0);
        allValid = allValid && ok;
        std::cout << "GDSRawFileReader: chunk " << i << " offset " << offset << " size " << toRead
                  << (ok ? "  VALID" : "  MISMATCH") << std::endl;
      }
    }
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = end - start;
    std::cout << "GDSRawFileReader: GDS read " << totalBytes << " bytes in " << numChunks << " chunks, "
              << elapsed.count() * 1e3 << " ms, " << (totalBytes / 1e9) / elapsed.count() << " GB/s" << std::endl;

    if (validate_) {
      std::cout << "GDSRawFileReader: validation " << (allValid ? "PASSED (all chunks byte exact)" : "FAILED")
                << std::endl;
      cudaCheck(cudaFreeHost(valDisk));
      cudaCheck(cudaFreeHost(valDev));
      ::close(valFd);
    }

    cuFileBufDeregister(devPtr);
    cuFileHandleDeregister(cfHandle);
    ::close(fd);
    cuFileDriverClose();

  } else {
    int fd = ::open(inputFile_.c_str(), O_RDONLY);
    if (fd < 0)
      return;

    unsigned char* hostChunk = nullptr;
    cudaCheck(cudaMallocHost((void**)&hostChunk, chunkSize));  // pinned

    size_t totalBytes = 0;
    auto start = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < numChunks; ++i) {
      const off_t offset = off_t(i) * off_t(chunkSize);
      const size_t toRead = std::min(chunkSize, fileSize - size_t(offset));
      // read the chunk in a loop
      size_t done = 0;
      while (done < toRead) {
        ssize_t got = ::pread(fd, hostChunk + done, toRead - done, offset + done);
        if (got <= 0)
          break;
        done += got;
      }
      cudaCheck(cudaMemcpy(devPtr, hostChunk, done, cudaMemcpyHostToDevice));
      totalBytes += done;
      if (i == 0)
        printFirstEvent(hostChunk, rawHeaderSize);
    }
    cudaCheck(cudaDeviceSynchronize());
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = end - start;
    std::cout << "GDSRawFileReader: POSIX+H2D read " << totalBytes << " bytes in " << numChunks << " chunks, "
              << elapsed.count() * 1e3 << " ms, " << (totalBytes / 1e9) / elapsed.count() << " GB/s" << std::endl;

    cudaCheck(cudaFreeHost(hostChunk));
    ::close(fd);
  }

  cudaCheck(cudaFreeHost(hostPeek));
  cudaCheck(cudaFree(devPtr));
  std::cout << "GDSRawFileReader: done." << std::endl;
}

#include "FWCore/Framework/interface/MakerMacros.h"
DEFINE_FWK_MODULE(GDSRawFileReader);
