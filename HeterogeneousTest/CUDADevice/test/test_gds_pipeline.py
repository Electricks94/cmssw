import glob
import os
import sys
import FWCore.ParameterSet.Config as cms

# usage: cmsRun test_gds_pipeline.py gds_flag [input_dir_or_file] [max_events]

if len(sys.argv) < 2:
    raise RuntimeError("Usage: cmsRun test_gds_pipeline.py gds_flag [input_dir_or_file] [max_events]")

gds_flag = bool(int(sys.argv[1]))
input_path = sys.argv[2] if len(sys.argv) > 2 else \
    "/scratch/CMSSW_20_1_X_2026-08-08-1100/src/store/run402360"
max_events = int(sys.argv[3]) if len(sys.argv) > 3 else -1

if os.path.isdir(input_path):
    input_files = sorted(glob.glob(os.path.join(input_path, "*.raw")))
    if not input_files:
        raise RuntimeError("no .raw files found in %s" % input_path)
else:
    input_files = [input_path]

print("test_gds_pipeline: %d input file(s), first %s" % (len(input_files), os.path.basename(input_files[0])))

process = cms.Process('GDSPIPE')
process.load('HeterogeneousCore.CUDACore.ProcessAcceleratorCUDA_cfi')

process.source = cms.Source('GDSRawFileReaderPure',
    inputFiles = cms.vstring(input_files),
    useGDS = cms.bool(gds_flag),
    produceLegacy = cms.bool(False),
    validate = cms.bool(False),
    verbose = cms.bool(True),
    nSlots = cms.uint32(3)
)

process.pipeline = cms.EDAnalyzer('GDSPixelGatherAnalyzer',
    deviceSource = cms.InputTag('rawDataCollector'),
    minFedId = cms.uint32(1200),
    maxFedId = cms.uint32(1349),
    dumpDigis = cms.uint32(0),
    printEvery = cms.uint32(0)     # e.g. 500 for periodic per-event lines
)

process.path = cms.Path(process.pipeline)
process.maxEvents.input = max_events

# framework-level event rate, same metric as the hlt_alpaka.py runs (ev/s)
process.ThroughputService = cms.Service('ThroughputService',
    enableDQM = cms.untracked.bool(False),
    eventRange = cms.untracked.uint32(100000),
    eventResolution = cms.untracked.uint32(1000),
    printEventSummary = cms.untracked.bool(True)
)
