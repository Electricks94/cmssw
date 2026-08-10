import sys
import FWCore.ParameterSet.Config as cms

if len(sys.argv) < 2:
    raise RuntimeError("Usage: cmsRun test_gds_device.py gds_flag [input_file] [max_events]")

gds_flag = bool(int(sys.argv[1]))
input_file = sys.argv[2] if len(sys.argv) > 2 else \
    "/scratch/CMSSW_20_1_X_2026-07-20-1100/src/store/run402360/run402360_ls0214_index000000.raw"
max_events = int(sys.argv[3]) if len(sys.argv) > 3 else 10

process = cms.Process('GDSDEVICE')
process.load('HeterogeneousCore.CUDACore.ProcessAcceleratorCUDA_cfi')

process.source = cms.Source('GDSRawFileReaderPure',
    inputFile = cms.string(input_file),
    useGDS = cms.bool(gds_flag)
)

process.pixelGather = cms.EDAnalyzer('GDSPixelGatherAnalyzer',
    deviceSource = cms.InputTag('rawDataCollector'),
    legacySource = cms.InputTag('rawDataCollector'),
    minFedId = cms.uint32(1200),
    maxFedId = cms.uint32(1349),
    validate = cms.bool(True)
)

process.path = cms.Path(process.pixelGather)
process.maxEvents.input = max_events

