import sys
import FWCore.ParameterSet.Config as cms

# usage: cmsRun test_gds.py gds_flag [chunk_mb] [input_file] [validate]
#   gds_flag   1 = cuFileRead into device memory, 0 = POSIX read + H2D copy
#   chunk_mb   chunk size in MB (default 200, same as the HLT source config)
#   input_file path to a .raw file (default: first file of run 402360)
#   validate   1 = per chunk byte comparison vs pread (GDS path only, slow)

if len(sys.argv) < 2:
    raise RuntimeError("Usage: cmsRun test_gds.py gds_flag [chunk_mb] [input_file] [validate]")

gds_flag = bool(int(sys.argv[1]))
chunk_mb = int(sys.argv[2]) if len(sys.argv) > 2 else 200
input_file = sys.argv[3] if len(sys.argv) > 3 else \
    "/scratch/test_felice/CMSSW_20_1_X_2026-07-07-1100/src/run/store/run402360/run402360_ls0214_index000000.raw"
validate_flag = bool(int(sys.argv[4])) if len(sys.argv) > 4 else False

process = cms.Process('TestGDSRawFileReader')
process.load('HeterogeneousCore.CUDACore.ProcessAcceleratorCUDA_cfi')

process.source = cms.Source('EmptySource')

process.gdsRawFileReader = cms.EDAnalyzer('GDSRawFileReader',
    inputFile = cms.string(input_file),
    chunkSizeMB = cms.uint32(chunk_mb),
    useGDS = cms.bool(gds_flag),
    validate = cms.bool(validate_flag)
)

process.path = cms.Path(process.gdsRawFileReader)

process.maxEvents.input = 1
