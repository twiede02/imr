#include "imr_private.h"

namespace imr {

struct AccelerationStructure::Impl {
    VkAccelerationStructureKHR handle;
    std::unique_ptr<Buffer> buffer;
    VkDeviceAddress deviceAddress;
};

AccelerationStructure::AccelerationStructure() {
    _impl = std::make_unique<Impl>();
}

void AccelerationStructure::createBuffer(Device& device, VkAccelerationStructureBuildSizesInfoKHR buildSizeInfo) {
    buffer = std::make_unique<imr::Buffer>(device, buildSizeInfo.accelerationStructureSize, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);
}

AccelerationStructure::~AccelerationStructure() {
}

}
