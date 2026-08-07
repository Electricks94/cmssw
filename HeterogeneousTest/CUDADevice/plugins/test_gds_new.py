import sys
import FWCore.ParameterSet.Config as cms

# usage: cmsRun test_gds_source.py gds_flag [input_file] [max_events]

if len(sys.argv) < 2:
    raise RuntimeError("Usage: cmsRun test_gds_source.py gds_flag [input_file] [max_events]")

gds_flag = bool(int(sys.argv[1]))
input_file = sys.argv[2] if len(sys.argv) > 2 else \
        "/scratch/CMSSW_20_1_X_2026-07-20-1100/src/store/run402360/run402360_ls0214_index000000.raw"
max_events = int(sys.argv[3]) if len(sys.argv) > 3 else 10

process = cms.Process('GDSSOURCE')
process.load('HeterogeneousCore.CUDACore.ProcessAcceleratorCUDA_cfi')

process.source = cms.Source('GDSRawFileReaderPure',
    inputFile = cms.string(input_file),
    useGDS = cms.bool(gds_flag)
)

process.dump = cms.EDAnalyzer('EventContentAnalyzer')
process.path = cms.Path(process.dump)

process.maxEvents.input = max_events
