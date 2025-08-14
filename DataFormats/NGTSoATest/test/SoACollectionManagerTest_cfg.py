import FWCore.ParameterSet.Config as cms

process = cms.Process("NGTSoATest")

process.load("FWCore.MessageService.MessageLogger_cfi")
# enable alpaka and GPU support
process.load("Configuration.StandardSequences.Accelerators_cff")
process.MessageLogger.cerr.FwkReport.reportEvery = 1

# Number of events
process.maxEvents = cms.untracked.PSet(input = cms.untracked.int32(1))

# Empty source
process.source = cms.Source("EmptySource")

# SoAProducer produces three PortableDeviceCollections
process.soaproducer = cms.EDProducer("SoAProducer@alpaka", soaParameter = cms.int32(42))
# MultiCollectionProducer creates a MultiCollectionManager that takes the PortableDeviceCollections
process.multiCollectionproducer = cms.EDProducer("MultiCollectionProducer@alpaka",
    soaInput1 = cms.InputTag("soaproducer", "SoAProduct1"),
    soaInput2 = cms.InputTag("soaproducer", "SoAProduct2"),
    soaInput3 = cms.InputTag("soaproducer", "SoAProduct3"),
)
# Producer that takes the MultiCollectionManager and launches a kernel that access all data of all SoAs
process.deviceconsumer = cms.EDProducer("DeviceConsumer@alpaka", 
                                        collectionManagerInput = cms.InputTag("multiCollectionproducer"))
# Add to process path
process.p = cms.Path(process.soaproducer + process.multiCollectionproducer + process.deviceconsumer)