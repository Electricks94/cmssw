#ifndef NGTSoATest_interface_DeviceCollectionTest_h
#define NGTSoATest_interface_DeviceCollectionTest_h

#include "DataFormats/NGTSoATest/interface/HostCollectionSoATest.h"
#include "DataFormats/Portable/interface/alpaka/PortableCollection.h"
#include "DataFormats/Portable/interface/PortableDeviceCollection.h"

#include "HeterogeneousCore/AlpakaInterface/interface/config.h"

namespace ALPAKA_ACCELERATOR_NAMESPACE {

    using SoADeviceCollection = std::conditional_t<std::is_same_v<Device, alpaka::DevCpu>,
                                              PortableCollection<SoA>,
                                              PortableDeviceCollection<SoA, Device>>;

    // using SoADeviceCollection = PortableCollection<SoA>; 
    using DeviceCollectionManager = MultiCollectionManager<SoADeviceCollection, 3>;
}

namespace cms::alpakatools {

  template <typename TDevice>
  struct CopyToHost<MultiCollectionManager<PortableDeviceCollection<SoA, TDevice>, 3>> {
    template <typename TQueue>
    static auto copyAsync(TQueue& queue, MultiCollectionManager<PortableDeviceCollection<SoA, TDevice>, 3> const& deviceData) {
      // PortableHostCollection<NGTSoA> hostData(deviceData.view().metadata().size() - 1, queue);
      // alpaka::memcpy(queue, hostData.buffer(), deviceData.buffer());
      printf("MultiCollectionManager: I'm copying to host.\n");

      return deviceData;
    }
  };

  template <>
  struct CopyToDevice<CollectionManager> {
    template <typename TQueue>
    static auto copyAsync(TQueue& queue, CollectionManager const& deviceData) {
      // PortableHostCollection<NGTSoA> hostData(deviceData.view().metadata().size() - 1, queue);
      // alpaka::memcpy(queue, hostData.buffer(), deviceData.buffer());
      printf("MultiCollectionManager: I'm copying to device.\n");
      return deviceData;
    }
  };


}  // namespace cms::alpakatools

ASSERT_DEVICE_MATCHES_HOST_COLLECTION(SoADeviceCollection, SoAHostCollection)


#endif  // NGTSoATest_interface_DeviceCollectionTest_h
