import glob
import os
import sys
import FWCore.ParameterSet.Config as cms

# usage: cmsRun test_gds_pipeline.py gds_flag [input_dir_or_file] [max_events] [threads] [streams]
#   gds_flag    1 = cuFileRead into device memory, 0 = POSIX read + H2D copy
#   second arg  a DIRECTORY -> every *.raw inside it, sorted;  a FILE -> just that file
#   max_events  -1 processes everything
#   threads/streams  EDM concurrency (default 32/32, use 1/1 for the serial baseline)

if len(sys.argv) < 2:
    raise RuntimeError(
        "Usage: cmsRun test_gds_pipeline.py gds_flag [input_dir_or_file] [max_events] [threads] [streams]")

gds_flag = bool(int(sys.argv[1]))
input_path = sys.argv[2] if len(sys.argv) > 2 else \
    "/scratch/CMSSW_20_1_X_2026-08-11-1100/src/store/run402360"
max_events = int(sys.argv[3]) if len(sys.argv) > 3 else -1
n_threads = int(sys.argv[4]) if len(sys.argv) > 4 else 32
n_streams = int(sys.argv[5]) if len(sys.argv) > 5 else 32

if os.path.isdir(input_path):
    input_files = sorted(glob.glob(os.path.join(input_path, "*.raw")))
    if not input_files:
        raise RuntimeError("no .raw files found in %s" % input_path)
elif os.path.isfile(input_path):
    input_files = [input_path]
else:
    raise RuntimeError("%s is neither a directory nor a file" % input_path)

print("test_gds_pipeline: %d file(s), %d threads, %d streams, maxEvents %d"
      % (len(input_files), n_threads, n_streams, max_events))

process = cms.Process('GDSPIPE')
process.load('HeterogeneousCore.CUDACore.ProcessAcceleratorCUDA_cfi')

# ----------------------------------------------------------------- concurrency
process.options.numberOfThreads = n_threads
process.options.numberOfStreams = n_streams
# the dataset spans several lumisections; keep lumi handling simple
process.options.numberOfConcurrentLuminosityBlocks = 1
process.options.wantSummary = False

# ---------------------------------------------------------------------- source
# nSlots is the device buffer ring depth. A slot is only reused nSlots files
# later, so it must outlive every event still in flight from that file:
#   nSlots > (events in flight) / (events per file) + 1
# With ~100 events per file and <= n_streams in flight, 4 is comfortable.
process.source = cms.Source('GDSRawFileReaderPure',
    inputFiles = cms.vstring(input_files),
    useGDS = cms.bool(gds_flag),
    produceLegacy = cms.bool(False),   # device product only: no host mirror, no host copies
    validate = cms.bool(False),        # True to byte-check the scan on every file (slow)
    verbose = cms.bool(False),         # True for one line per file
    nSlots = cms.uint32(4)
)

# -------------------------------------------------------------------- pipeline
process.pipeline = cms.EDAnalyzer('GDSPixelGatherAnalyzer',
    deviceSource = cms.InputTag('rawDataCollector'),
    minFedId = cms.uint32(1200),
    maxFedId = cms.uint32(1349),
    dumpDigis = cms.uint32(0),
    printEvery = cms.uint32(0),        # e.g. 5000 for periodic per-event lines
    maxStreams = cms.uint32(32),       # must be >= numberOfStreams
    maxFeds = cms.uint32(4096),
    maxWords = cms.uint32(262144)
)

# -------------------------------------------------------------- memory monitor
# Consumes nothing, so the identical module can be appended to hlt_alpaka.py's
# path to measure the production pipeline with the same instrument.
process.mem = cms.EDAnalyzer('GPUMemMonitor',
    label = cms.string('gds'),
    sampleEvery = cms.uint32(100)      # cudaMemGetInfo is ~20 us; 1 for short runs
)

process.path = cms.Path(process.pipeline + process.mem)
process.maxEvents.input = max_events

# ------------------------------------------------------------------- reporting
# framework event rate, the same metric as the hlt_alpaka.py runs
process.ThroughputService = cms.Service('ThroughputService',
    enableDQM = cms.untracked.bool(False),
    eventRange = cms.untracked.uint32(100000),
    eventResolution = cms.untracked.uint32(1000),
    printEventSummary = cms.untracked.bool(True)
)

# stop the per-event "Begin processing the Nth record" spam ...
process.MessageLogger.cerr.FwkReport.reportEvery = 1000
# ... and let the ThroughputService lines through, as hlt.py does
process.MessageLogger.cerr.enableStatistics = cms.untracked.bool(False)
process.MessageLogger.cerr.ThroughputService = cms.untracked.PSet(
    limit = cms.untracked.int32(10000000),
    reportEvery = cms.untracked.int32(1)
)
