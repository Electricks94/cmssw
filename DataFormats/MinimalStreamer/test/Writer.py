import FWCore.ParameterSet.Config as cms

process = cms.Process("Writer")
process.source = cms.Source('EmptySource')
process.maxEvents = cms.untracked.PSet(input = cms.untracked.int32(1))

process.load("FWCore.MessageService.MessageLogger_cfi")
process.MessageLogger.cerr.FwkReport.reportEvery = 1

# enable logging for the analysers
process.MessageLogger.soAAnalyzer = cms.untracked.PSet()

# Produce zeroth evolution of the SoA product
process.minimalStreamerProducer = cms.EDProducer("MinimalStreamerProducer")

# Analyze the produced product right away
process.minimalStreamerAnalyzer = cms.EDAnalyzer('MinimalStreamerAnalyzer',
    source = cms.InputTag("minimalStreamerProducer", "TestProduct"),
)

# write all products to a 'test.root' file
process.output = cms.OutputModule('PoolOutputModule',
    fileName = cms.untracked.string('test.root'),
   outputCommands = cms.untracked.vstring('keep *')
)

# Add to process path
process.p = cms.Path(process.minimalStreamerProducer + process.minimalStreamerAnalyzer)
process.output_path = cms.EndPath(process.output)