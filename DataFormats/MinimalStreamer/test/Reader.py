import FWCore.ParameterSet.Config as cms

process = cms.Process('Reader')
process.maxEvents = cms.untracked.PSet(input = cms.untracked.int32(1))

# read the products from a 'testOne.root' file
process.source = cms.Source('PoolSource',
    fileNames = cms.untracked.vstring('file:test.root')
)

# enable logging for the analyser
process.MessageLogger.soAAnalyzer = cms.untracked.PSet()

process.minimalStreamerAnalyzer = cms.EDAnalyzer('MinimalStreamerAnalyzer',
    source = cms.InputTag("minimalStreamerProducer", "TestProduct"),
)

process.p = cms.Path(process.minimalStreamerAnalyzer)