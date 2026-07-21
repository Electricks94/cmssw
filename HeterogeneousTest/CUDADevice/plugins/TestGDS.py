import FWCore.ParameterSet.Config as cms

if len(sys.argv) < 3:
    raise RuntimeError("Usage: cmsRun testGDS.py array_elements gds_flag")

array_elements = int(sys.argv[1])
gds_flag = bool(int(sys.argv[2]))

process = cms.Process('TestCUDATestDeviceAdditionModule')
process.load('HeterogeneousCore.CUDACore.ProcessAcceleratorCUDA_cfi')

process.source = cms.Source('EmptySource')

process.cudaTestDeviceAdditionModule = cms.EDAnalyzer('CUDAstorage',
    size = cms.uint32( array_elements ),
    benchmarkGDS = cms.bool(gds_flag),
    outputFilename = cms.string("output")
)

process.path = cms.Path(process.cudaTestDeviceAdditionModule)

process.maxEvents.input = 1
