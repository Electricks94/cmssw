#ifndef NGTSoATest_interface_HostCollectionTest_h
#define NGTSoATest_interface_HostCollectionTest_h

#include "CommonTools/RecoAlgos/interface/MultiCollectionManager.h"
#include "DataFormats/NGTSoATest/interface/SoALayoutTest.h"
#include "DataFormats/Portable/interface/PortableHostCollection.h"
#include "DataFormats/Portable/interface/MultiSoAViewManager.h"

using SoAHostCollection = PortableHostCollection<SoA>;
using CollectionManager = MultiCollectionManager<SoAHostCollection, 3>;

#endif // NGTSoATest_interface_HostCollectionTest_h
