# baseline_mem.py
import FWCore.ParameterSet.Config as cms
process = cms.Process('BASE')
process.load('HeterogeneousCore.CUDACore.ProcessAcceleratorCUDA_cfi')
process.source = cms.Source('EmptySource')
process.mem = cms.EDAnalyzer('GPUMemMonitor', label=cms.string('baseline'),
                             sampleEvery=cms.uint32(1))
process.path = cms.Path(process.mem)
process.maxEvents.input = 10
