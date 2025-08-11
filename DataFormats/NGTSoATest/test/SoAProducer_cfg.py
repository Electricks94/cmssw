import FWCore.ParameterSet.Config as cms
# from DataFormats.NGTSoATest.soAProducer4_cfi import soAProducer4

process = cms.Process("SoAProducer")

process.load("FWCore.MessageService.MessageLogger_cfi")
process.load("Configuration.StandardSequences.Accelerators_cff")
process.MessageLogger.cerr.FwkReport.reportEvery = 1

# Number of events
process.maxEvents = cms.untracked.PSet(input = cms.untracked.int32(1))

# Empty source
process.source = cms.Source("EmptySource")

# Add the producer
process.soaproducer = cms.EDProducer("SoAProducer", soaParameter = cms.int32(42))
process.multiCollectionproducer = cms.EDProducer("MultiCollectionProducer",
                                                 soaInput1 = cms.InputTag("soaproducer", "SoAProduct1"),
                                                 soaInput2 = cms.InputTag("soaproducer", "SoAProduct2"),
                                                 soaInput3 = cms.InputTag("soaproducer", "SoAProduct3"))

process.multiCollectionconsumer = cms.EDProducer("MultiCollectionConsumer", 
                                                 collectionManagerInput = cms.InputTag("multiCollectionproducer", "CollectionManager"))
#process.soaproducer4 = soAProducer4.clone(soaInput_2 = cms.InputTag("soaproducer3", "SoAProduct3"))
'''
process.soaproducer4 = cms.EDProducer("SoAProducer4@alpaka",
    soaInput_2 = cms.InputTag("soaproducer3", "SoAProduct3"),
    alpaka = cms.untracked.PSet(
        backend = cms.untracked.string("serial_sync")
    )
)
'''

# Add to process path
process.p = cms.Path(process.soaproducer * process.multiCollectionproducer * process.multiCollectionconsumer)