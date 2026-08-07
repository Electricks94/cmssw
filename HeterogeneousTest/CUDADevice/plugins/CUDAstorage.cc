#include <cstddef>
#include <cstdint>
#include <iostream>
#include <fcntl.h>
#include <random>
#include <vector>
#include <fstream>
#include <chrono>

#include <cuda_runtime.h>
// CUDA GDS file
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

using namespace cms::cuda;
#define cudaCheck(ARG) cms::cuda::cudaCheck(__FILE__, __LINE__, __func__, (ARG))

class CUDAstorage : public edm::global::EDAnalyzer<> {
public:
  explicit CUDAstorage(edm::ParameterSet const& config);
  ~CUDAstorage() override = default;

  static void fillDescriptions(edm::ConfigurationDescriptions& descriptions);

  void analyze(edm::StreamID, edm::Event const& event, edm::EventSetup const& setup) const override;

private:
  const uint32_t size_;
  const bool benchmarkGDS_;
  const std::string outputFilename_;
};

CUDAstorage::CUDAstorage(edm::ParameterSet const& config)
    : size_(config.getParameter<uint32_t>("size")),
      benchmarkGDS_(config.getParameter<bool>("benchmarkGDS")),
      outputFilename_(config.getParameter<std::string>("outputFilename")) {}

void CUDAstorage::fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
  edm::ParameterSetDescription desc;
  desc.add<uint32_t>("size");
  desc.add<bool>("benchmarkGDS");
  desc.add<std::string>("outputFilename");
  descriptions.addWithDefaultLabel(desc);
}

void CUDAstorage::analyze(edm::StreamID, edm::Event const& event, edm::EventSetup const& setup) const {
  // require CUDA for running
  edm::Service<CUDAInterface> cuda;
  if (not cuda or not cuda->enabled()) {
    std::cout << "The CUDAService is not available or disabled, the test will be skipped.\n";
    return;
  }

  std::cout << "CUDAstorage Module: Cuda Setup success, starting with memory allocation" << std::endl;

  const size_t bufferSize = size_ * sizeof(float);
  const std::string deviceOutputFilename = outputFilename_ + "_device_" + std::to_string(bufferSize) + ".data";
  const std::string hostOutputFilename = outputFilename_ + "_host_" +std::to_string(bufferSize) + ".data";

  // random number generator with a gaussian distribution
  std::random_device rd{};
  std::default_random_engine rand{rd()};
  std::normal_distribution<float> dist{0., 1.};

  std::vector<float> referenceArray(size_);

  // allocate input and output host buffers
  float* hostData;
  cudaCheck(cudaMallocHost((void **) &hostData, bufferSize));

  // fill the input buffers with random data, and the output buffer with zeros
  for (size_t i = 0; i < size_; ++i) {
    hostData[i] = dist(rand);
    referenceArray[i] = hostData[i];
  }

  // allocate input and output buffers on the device
  float* devPtr;
  cudaCheck(cudaMalloc(&devPtr, bufferSize));
  cudaCheck(cudaMemcpy(devPtr, hostData, bufferSize, cudaMemcpyHostToDevice));
  cudaCheck(cudaDeviceSynchronize());

  if (benchmarkGDS_) {
    std::cout << "CUDAstorage Module: Starting GDS setup and benchmark" << std::endl;
    std::cout << "CUDAstorage Module: Creating CUfileHandle" << std::endl;

    // Cuda I/O
    CUfileHandle_t cfHandle;
    CUfileDescr_t cfDescr = {};
    int fd = open(deviceOutputFilename.c_str(), O_CREAT | O_RDWR, 0664);
    if (fd < 0) {
      std::cout << "File open failed! Returning." << std::endl;
      return;
    }

    std::cout << "CUDAstorage Module: Set up GDS descriptor" << std::endl;

    // Set up GDS descriptor
    cfDescr.handle.fd = fd;
    cfDescr.type = CU_FILE_HANDLE_TYPE_OPAQUE_FD;
    CUfileError_t status = cuFileHandleRegister(&cfHandle, &cfDescr);
    if (status.err != CU_FILE_SUCCESS) {
      std::cout << "cuFileHandleRegister failed: " << status.err << std::endl;
      close(fd);
      return;
    }

    std::cout << "CUDAstorage Module: Registering device memory of size :" << bufferSize << std::endl;
    status = cuFileBufRegister(devPtr, bufferSize, 0);
    if (status.err != CU_FILE_SUCCESS) {
      std::cout << "buffer register failed: " << status.err << std::endl;
      return;
    }

    std::cout << "CUDAstorage Module: Writing from device memory" << std::endl;
    // Perform the write
    auto start = std::chrono::high_resolution_clock::now();
    ssize_t writtenBytes = cuFileWrite(cfHandle, devPtr, bufferSize, 0, 0);
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> elapsed = end - start;
    std::cout << "Time storage GDS: " << elapsed.count() << " ms\n";

    if (writtenBytes < 0) {
      std::cout << "cuFileWrite failed!" << std::endl;
      return;
    } else {
      std::cout << "Wrote " << writtenBytes << " bytes to the file." << std::endl;
    }
    // Clean up
    status = cuFileBufDeregister(devPtr);
    if (status.err != CU_FILE_SUCCESS) {
      std::cout << "buffer deregister failed: " << status.err << std::endl;
      return;
    }

    cudaCheck(cudaFree(devPtr));
    cudaCheck(cudaFreeHost(hostData));

    cuFileHandleDeregister(cfHandle);
    close(fd);

    /*
    status = cuFileDriverClose();
    if (status.err != CU_FILE_SUCCESS) {
      std::cout << "cufile driver close failed: " << status.err << std::endl;
      return;
    }
    */

  } else {
    std::cout << "CUDAstorage Module: No GDS run, Syncing data to CPU and writing buffer" << std::endl;


    std::ofstream outFile(hostOutputFilename, std::ios::binary);

    // Check if the file is open
    if (!outFile.is_open()) {
      std::cerr << "CUDAstorage Module: Error opening file for writing!" << std::endl;
      return;
    }

    auto start = std::chrono::high_resolution_clock::now();
    cudaCheck(cudaMemcpy(hostData, devPtr, bufferSize, cudaMemcpyDeviceToHost));
    cudaCheck(cudaDeviceSynchronize());
    // Write the vector data
    outFile.write(reinterpret_cast<const char*>(hostData), bufferSize);
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> elapsed = end - start;
    std::cout << "Time storage CPU: " << elapsed.count() << " ms\n";

    outFile.close();

    std::cout << "CUDAstorage Module: Wrote Data from CPU!" << std::endl;

    cudaCheck(cudaFree(devPtr));
    cudaCheck(cudaFreeHost(hostData));
  }

  std::cout << "CUDAstorage Module: Writing Success!" << std::endl;

  if (benchmarkGDS_) {
    int fd = open(deviceOutputFilename.c_str(), O_RDONLY | O_DIRECT);
    if (fd < 0) {
      std::cout << "File open failed! Returning." << std::endl;
      return;
    }

    CUfileHandle_t cfHandle;
    CUfileDescr_t cfDescr = {};
    memset((void *)&cfDescr, 0, sizeof(CUfileDescr_t));
    cfDescr.handle.fd = fd;
    cfDescr.type = CU_FILE_HANDLE_TYPE_OPAQUE_FD;
    CUfileError_t status = cuFileHandleRegister(&cfHandle, &cfDescr);

    float* devReadPtr;
    cudaCheck(cudaMalloc(&devReadPtr, bufferSize));
    cudaCheck(cudaDeviceSynchronize());

    auto start = std::chrono::high_resolution_clock::now();
    cuFileRead(cfHandle, devReadPtr, bufferSize, 0, 0);
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> elapsed = end - start;
    std::cout << "Time read GDS: " << elapsed.count() << " ms\n";

    cuFileHandleDeregister(cfHandle);
	  close (fd);

    float* hostDataRead;
    cudaCheck(cudaMallocHost((void **) &hostDataRead, bufferSize));
    cudaCheck(cudaMemcpy(hostDataRead, devReadPtr, bufferSize, cudaMemcpyDeviceToHost));
    cudaCheck(cudaDeviceSynchronize());

    float epsilon = 1e-6;
    for (size_t i = 0; i < size_; ++i) {
      if(std::fabs(hostDataRead[i] - referenceArray[i]) > epsilon){
                std::cout << "Data mismatch at index " << i << ": expected " << referenceArray[i]
                  << ", got " << hostDataRead[i] << std::endl;

        return;
      }
    }

    std::cout << "CUDAstorage Module: GDS Data verification success!" << std::endl;

    cudaCheck(cudaFree(devReadPtr));
    cudaCheck(cudaFreeHost(hostDataRead));

    status = cuFileDriverClose();

  }
  else{

    float* hostDataRead;
    cudaCheck(cudaMallocHost((void **) &hostDataRead, bufferSize));

    float* devReadPtr;
    cudaCheck(cudaMalloc(&devReadPtr, bufferSize));
    cudaCheck(cudaDeviceSynchronize());

    std::ifstream inFile(hostOutputFilename, std::ios::binary);

     auto start = std::chrono::high_resolution_clock::now();
    inFile.read(reinterpret_cast<char*>(hostDataRead), bufferSize);
    cudaCheck(cudaMemcpy(devReadPtr, hostDataRead, bufferSize, cudaMemcpyHostToDevice));
    cudaCheck(cudaDeviceSynchronize());
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> elapsed = end - start;
    std::cout << "Time read CPU: " << elapsed.count() << " ms\n";


    inFile.close();

  }

  std::cout << "CUDAstorage Module success.\n";
}

#include "FWCore/Framework/interface/MakerMacros.h"
DEFINE_FWK_MODULE(CUDAstorage);
