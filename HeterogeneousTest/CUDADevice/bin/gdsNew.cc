#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <random>
#include <fstream>
#include <iostream>
#include <chrono>

#include <fcntl.h>
#include <unistd.h>

#include <cuda_runtime.h>
#include "cufile.h"

#define CUDA_CHECK(call)                                                      \
  do {                                                                        \
    cudaError_t _e = (call);                                                  \
    if (_e != cudaSuccess) {                                                  \
      std::cerr << "CUDA error " << cudaGetErrorString(_e) << " at "          \
                << __FILE__ << ":" << __LINE__ << std::endl;                  \
      std::exit(1);                                                           \
    }                                                                         \
  } while (0)

static bool cufileOk(CUfileError_t s, const char* what){
  if (s.err != CU_FILE_SUCCESS) {
    std::cerr << what << " failed, cuFile err = " << s.err << std::endl;
    return false;
  }
  return true;
}

static double gib_per_sec(size_t bytes, double ms){
    double sec = ms/1000.0;
    if (sec <= 0) return 0.0;
    return (double)bytes/sec/(1024.0*1024.0*1024.0);
}

static double gb_per_sec(size_t bytes, double ms){
    double sec = ms/1000.0;
    if (sec <= 0) return 0.0;
    return (double)bytes/sec/1.0e9;
}


static void report(const char* label, size_t bytes, double ms) {
  std::cout << label << ": " << ms << " ms  |  throughput: "
            << gib_per_sec(bytes, ms) << " GiB/s (" << gb_per_sec(bytes, ms)
            << " GB/s)  over " << bytes << " bytes\n";
}

static size_t parseSize(const std::string& s) {
  if (s.empty()) return 0;
  char u = s.back();
  double mult = 1.0;
  std::string num = s;
  if (u == 'K' || u == 'k') { mult = 1024.0; num = s.substr(0, s.size() - 1); }
  else if (u == 'M' || u == 'm') { mult = 1024.0 * 1024.0; num = s.substr(0, s.size() - 1); }
  else if (u == 'G' || u == 'g') { mult = 1024.0 * 1024.0 * 1024.0; num = s.substr(0, s.size() - 1); }
  return (size_t)(std::stod(num) * mult);
}

static void usage(const char* prog) {
  std::cout << "usage: " << prog << " [-s SIZE] [-m gds|cpu] [-o OUTBASE] "
            << "[-i INPUTFILE] [-d GPU]\n"
            << "  -s SIZE   total buffer, K/M/G suffix (default 1G)\n"
            << "  -m MODE   gds (default) or cpu\n"
            << "  -o OUTBASE output path base (default /scratch/cudastorage)\n"
            << "  -i INPUT  load source bytes from this file (reuse test data)\n"
            << "  -d GPU    cuda device index (default 0)\n";
}

int main(int argc, char** argv) {
  size_t bytes = 1024ull * 1024 * 1024;   // 1 GiB default
  std::string mode = "gds";
  std::string outBase = "/scratch/cudastorage";
  std::string inputFile;
  int gpu = 0;

  int opt;
  while ((opt = getopt(argc, argv, "s:m:o:i:d:h")) != -1) {
    switch (opt) {
      case 's': bytes = parseSize(optarg); break;
      case 'm': mode = optarg; break;
      case 'o': outBase = optarg; break;
      case 'i': inputFile = optarg; break;
      case 'd': gpu = std::stoi(optarg); break;
      case 'h': default: usage(argv[0]); return 0;
    }
  }

  const bool benchmarkGDS = (mode != "cpu");

  // align down to 4K so O_DIRECT read has aligned size, and to whole floats
  bytes = (bytes / 4096) * 4096;
  if (bytes == 0) { std::cerr << "size too small\n"; return 1; }
  const size_t nfloats = bytes / sizeof(float);

  CUDA_CHECK(cudaSetDevice(gpu));

  const std::string deviceOutputFilename = outBase + "_device_" + std::to_string(bytes) + ".data";
  const std::string hostOutputFilename   = outBase + "_host_"   + std::to_string(bytes) + ".data";

  std::cout << "mode: " << (benchmarkGDS ? "GDS (cuFile)" : "CPU staging")
            << "   size: " << bytes << " bytes (" << (bytes / (1024.0 * 1024.0 * 1024.0))
            << " GiB)   gpu: " << gpu << "\n";

  float* hostData = nullptr;
  CUDA_CHECK(cudaMallocHost((void**)&hostData, bytes));

  std::vector<char> reference(bytes);  // byte exact copy for verification

  if (!inputFile.empty()) {
    std::ifstream in(inputFile, std::ios::binary);
    if (!in) { std::cerr << "cannot open input file " << inputFile << "\n"; return 1; }
    in.read(reinterpret_cast<char*>(hostData), bytes);
    std::streamsize got = in.gcount();
    std::cout << "loaded " << got << " bytes from " << inputFile << "\n";
    if ((size_t)got < bytes) {
      std::cout << "input smaller than buffer; padding remainder with random data\n";
      std::default_random_engine rnd{std::random_device{}()};
      std::normal_distribution<float> dist{0.f, 1.f};
      for (size_t i = (size_t)got / sizeof(float); i < nfloats; ++i) hostData[i] = dist(rnd);
    }
  } else {
    std::default_random_engine rnd{std::random_device{}()};
    std::normal_distribution<float> dist{0.f, 1.f};
    for (size_t i = 0; i < nfloats; ++i) hostData[i] = dist(rnd);
  }
  std::memcpy(reference.data(), hostData, bytes);

  float* devPtr = nullptr;
  CUDA_CHECK(cudaMalloc((void**)&devPtr, bytes));
  CUDA_CHECK(cudaMemcpy(devPtr, hostData, bytes, cudaMemcpyHostToDevice));
  CUDA_CHECK(cudaDeviceSynchronize());

  if (benchmarkGDS) {
    //gds write
    int fd = open(deviceOutputFilename.c_str(), O_CREAT | O_RDWR, 0664);
    if (fd < 0) { std::cerr << "open for write failed\n"; return 1; }

    CUfileDescr_t cfDescr = {};
    cfDescr.handle.fd = fd;
    cfDescr.type = CU_FILE_HANDLE_TYPE_OPAQUE_FD;
    CUfileHandle_t cfHandle;
    if (!cufileOk(cuFileHandleRegister(&cfHandle, &cfDescr), "cuFileHandleRegister(write)")) { close(fd); return 1; }
    if (!cufileOk(cuFileBufRegister(devPtr, bytes, 0), "cuFileBufRegister")) return 1;

    auto t0 = std::chrono::high_resolution_clock::now();
    ssize_t wrote = cuFileWrite(cfHandle, devPtr, bytes, 0, 0);
    auto t1 = std::chrono::high_resolution_clock::now();
    double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    if (wrote < 0) { std::cerr << "cuFileWrite failed\n"; return 1; }
    report("Time storage GDS", (size_t)wrote, ms);

    cufileOk(cuFileBufDeregister(devPtr), "cuFileBufDeregister");
    cuFileHandleDeregister(cfHandle);
    close(fd);
    CUDA_CHECK(cudaFree(devPtr));

    // gds read
    fd = open(deviceOutputFilename.c_str(), O_RDONLY | O_DIRECT);
    if (fd < 0) { std::cerr << "open for read failed\n"; return 1; }

    CUfileDescr_t cfDescrR = {};
    cfDescrR.handle.fd = fd;
    cfDescrR.type = CU_FILE_HANDLE_TYPE_OPAQUE_FD;
    CUfileHandle_t cfHandleR;
    if (!cufileOk(cuFileHandleRegister(&cfHandleR, &cfDescrR), "cuFileHandleRegister(read)")) { close(fd); return 1; }

    float* devReadPtr = nullptr;
    CUDA_CHECK(cudaMalloc((void**)&devReadPtr, bytes));
    CUDA_CHECK(cudaDeviceSynchronize());

    auto r0 = std::chrono::high_resolution_clock::now();
    ssize_t rd = cuFileRead(cfHandleR, devReadPtr, bytes, 0, 0);
    auto r1 = std::chrono::high_resolution_clock::now();
    double rms = std::chrono::duration<double, std::milli>(r1 - r0).count();
    if (rd < 0) { std::cerr << "cuFileRead failed\n"; return 1; }
    report("Time read GDS", (size_t)rd, rms);

    cuFileHandleDeregister(cfHandleR);
    close(fd);

    // verify byte exact
    float* hostRead = nullptr;
    CUDA_CHECK(cudaMallocHost((void**)&hostRead, bytes));
    CUDA_CHECK(cudaMemcpy(hostRead, devReadPtr, bytes, cudaMemcpyDeviceToHost));
    CUDA_CHECK(cudaDeviceSynchronize());
    if (std::memcmp(hostRead, reference.data(), bytes) == 0)
      std::cout << "GDS data verification success\n";
    else
      std::cout << "GDS data verification FAILED\n";

    CUDA_CHECK(cudaFree(devReadPtr));
    CUDA_CHECK(cudaFreeHost(hostRead));
    CUDA_CHECK(cudaFreeHost(hostData));
    cuFileDriverClose();

} else {
    // cpu write
    std::ofstream outFile(hostOutputFilename, std::ios::binary);
    if (!outFile.is_open()) { std::cerr << "cannot open host output file\n"; return 1; }
 
    auto t0 = std::chrono::high_resolution_clock::now();
    CUDA_CHECK(cudaMemcpy(hostData, devPtr, bytes, cudaMemcpyDeviceToHost));
    CUDA_CHECK(cudaDeviceSynchronize());
    outFile.write(reinterpret_cast<const char*>(hostData), bytes);
    auto t1 = std::chrono::high_resolution_clock::now();
    double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    outFile.close();
    report("Time storage CPU", bytes, ms);
 
    // cpu read
    float* hostRead = nullptr;
    CUDA_CHECK(cudaMallocHost((void**)&hostRead, bytes));
    float* devReadPtr = nullptr;
    CUDA_CHECK(cudaMalloc((void**)&devReadPtr, bytes));
    CUDA_CHECK(cudaDeviceSynchronize());
 
    std::ifstream inFile(hostOutputFilename, std::ios::binary);
    auto r0 = std::chrono::high_resolution_clock::now();
    inFile.read(reinterpret_cast<char*>(hostRead), bytes);
    CUDA_CHECK(cudaMemcpy(devReadPtr, hostRead, bytes, cudaMemcpyHostToDevice));
    CUDA_CHECK(cudaDeviceSynchronize());
    auto r1 = std::chrono::high_resolution_clock::now();
    double rms = std::chrono::duration<double, std::milli>(r1 - r0).count();
    inFile.close();
    report("Time read CPU", bytes, rms);
 
    if (std::memcmp(hostRead, reference.data(), bytes) == 0)
      std::cout << "CPU data verification success\n";
    else
      std::cout << "CPU data verification FAILED\n";
 
    CUDA_CHECK(cudaFree(devReadPtr));
    CUDA_CHECK(cudaFree(devPtr));
    CUDA_CHECK(cudaFreeHost(hostRead));
    CUDA_CHECK(cudaFreeHost(hostData));
  }
 
  std::cout << "done\n";
  return 0;


}
