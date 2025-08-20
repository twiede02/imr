#include "imr_private.h"

namespace imr {

struct AccelerationStructure::Impl {
    Device& device;
    VkAccelerationStructureKHR handle;
    std::unique_ptr<Buffer> buffer;
    VkDeviceAddress deviceAddress;

    void createAccelerationStructure(VkAccelerationStructureTypeKHR asType, std::vector<VkAccelerationStructureGeometryKHR>& geometries, std::vector<uint32_t> geometry_sizes);
};

AccelerationStructure::AccelerationStructure(Device& device) {
    _impl = std::make_unique<Impl>(device);
}

VkAccelerationStructureKHR AccelerationStructure::handle() const {
    return _impl->handle;
}

VkDeviceAddress AccelerationStructure::deviceAddress() const {
    return _impl->deviceAddress;
}

void AccelerationStructure::createBottomLevelAccelerationStructure(Device& device, Buffer& vertexBuffer, Buffer& indexBuffer, Buffer& transformBuffer) {
    auto& vk = device.dispatch;

    struct Vertex {
        float pos[3];
    };

    uint32_t indexCount = static_cast<uint32_t>(indexBuffer.size / sizeof(uint32_t));

    VkDeviceOrHostAddressConstKHR vertexBufferDeviceAddress{};
    VkDeviceOrHostAddressConstKHR indexBufferDeviceAddress{};
    VkDeviceOrHostAddressConstKHR transformBufferDeviceAddress{};

    vertexBufferDeviceAddress.deviceAddress = vertexBuffer.device_address();
    indexBufferDeviceAddress.deviceAddress = indexBuffer.device_address();
    transformBufferDeviceAddress.deviceAddress = transformBuffer.device_address();

    // Build
    VkAccelerationStructureGeometryKHR accelerationStructureGeometry{};
    accelerationStructureGeometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    accelerationStructureGeometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
    accelerationStructureGeometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
    accelerationStructureGeometry.geometry.triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
    accelerationStructureGeometry.geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
    accelerationStructureGeometry.geometry.triangles.vertexData = vertexBufferDeviceAddress;
    accelerationStructureGeometry.geometry.triangles.maxVertex = indexCount - 1;
    accelerationStructureGeometry.geometry.triangles.vertexStride = sizeof(Vertex);
    accelerationStructureGeometry.geometry.triangles.indexType = VK_INDEX_TYPE_UINT32;
    accelerationStructureGeometry.geometry.triangles.indexData = indexBufferDeviceAddress;
    accelerationStructureGeometry.geometry.triangles.transformData.deviceAddress = 0;
    accelerationStructureGeometry.geometry.triangles.transformData.hostAddress = nullptr;
    accelerationStructureGeometry.geometry.triangles.transformData = transformBufferDeviceAddress;

    std::vector<VkAccelerationStructureGeometryKHR> geometries = { accelerationStructureGeometry };
    _impl->createAccelerationStructure(VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR, geometries, { 1 });
}

void AccelerationStructure::createTopLevelAccelerationStructure(Device& device, std::vector<std::tuple<VkTransformMatrixKHR, AccelerationStructure*>>& bottomLevelAS)
{
    auto& vk = device.dispatch;

    std::vector<VkAccelerationStructureInstanceKHR> instances_cpu;
    for (auto [trans, as] : bottomLevelAS) {
        VkAccelerationStructureInstanceKHR instance{};
        instance.transform = trans;
        instance.instanceCustomIndex = 0;
        instance.mask = 0xFF;
        instance.instanceShaderBindingTableRecordOffset = 0;
        instance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
        instance.accelerationStructureReference = as->deviceAddress();
        instances_cpu.push_back(instance);
    }

    std::unique_ptr<imr::Buffer> instanceBuffer = std::make_unique<imr::Buffer>(device, sizeof(VkAccelerationStructureInstanceKHR) * bottomLevelAS.size(),
                VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, instances_cpu.data());

    VkDeviceOrHostAddressConstKHR instanceDataDeviceAddress{};
    instanceDataDeviceAddress.deviceAddress = instanceBuffer->device_address();

    VkAccelerationStructureGeometryKHR accelerationStructureGeometry{};
    accelerationStructureGeometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    accelerationStructureGeometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    accelerationStructureGeometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
    accelerationStructureGeometry.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    accelerationStructureGeometry.geometry.instances.arrayOfPointers = VK_FALSE;
    accelerationStructureGeometry.geometry.instances.data = instanceDataDeviceAddress;

    std::vector<VkAccelerationStructureGeometryKHR> oh_boy = { accelerationStructureGeometry };
    _impl->createAccelerationStructure(VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR, oh_boy, { (uint32_t) bottomLevelAS.size() });
}

// VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR

void AccelerationStructure::Impl::createAccelerationStructure(VkAccelerationStructureTypeKHR asType, std::vector<VkAccelerationStructureGeometryKHR>& geometries, std::vector<uint32_t> geometry_sizes)
{
    auto& vk = device.dispatch;

    // Get size info
    /*
       The pSrcAccelerationStructure, dstAccelerationStructure, and mode members of pBuildInfo are ignored. Any VkDeviceOrHostAddressKHR members of pBuildInfo are ignored by this command, except that the hostAddress member of VkAccelerationStructureGeometryTrianglesDataKHR::transformData will be examined to check if it is NULL.*
       */
    VkAccelerationStructureBuildGeometryInfoKHR accelerationStructureBuildGeometryInfo{};
    accelerationStructureBuildGeometryInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    accelerationStructureBuildGeometryInfo.type = asType;
    accelerationStructureBuildGeometryInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    // TODO
    accelerationStructureBuildGeometryInfo.geometryCount = geometries.size();
    accelerationStructureBuildGeometryInfo.pGeometries = geometries.data();

    VkAccelerationStructureBuildSizesInfoKHR accelerationStructureBuildSizesInfo{};
    accelerationStructureBuildSizesInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
    vk.getAccelerationStructureBuildSizesKHR(
            VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
            &accelerationStructureBuildGeometryInfo,
            geometry_sizes.data(),
            &accelerationStructureBuildSizesInfo);

    buffer = std::make_unique<imr::Buffer>(device, accelerationStructureBuildSizesInfo.accelerationStructureSize, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);

    VkAccelerationStructureCreateInfoKHR accelerationStructureCreateInfo{};
    accelerationStructureCreateInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    accelerationStructureCreateInfo.buffer = buffer->handle;
    accelerationStructureCreateInfo.size = accelerationStructureBuildSizesInfo.accelerationStructureSize;
    accelerationStructureCreateInfo.type = asType;
    vk.createAccelerationStructureKHR(&accelerationStructureCreateInfo, nullptr, &handle);

    accelerationStructureBuildGeometryInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    accelerationStructureBuildGeometryInfo.dstAccelerationStructure = handle;

    // Create a small scratch buffer used during build of the top level acceleration structure
    std::unique_ptr<imr::Buffer> scratchBuffer = std::make_unique<imr::Buffer>(device, accelerationStructureBuildSizesInfo.buildScratchSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);
    accelerationStructureBuildGeometryInfo.scratchData.deviceAddress = scratchBuffer->device_address();

    std::vector<std::unique_ptr<VkAccelerationStructureBuildRangeInfoKHR>> accelerationStructureBuildRangeInfos;
    std::vector<VkAccelerationStructureBuildRangeInfoKHR*> accelerationBuildStructureRangeInfos;
    for (auto geomCount : geometry_sizes) {
        // TODO
        VkAccelerationStructureBuildRangeInfoKHR accelerationStructureBuildRangeInfo = {
            .primitiveCount = geomCount,
            .primitiveOffset = 0,
            .firstVertex = 0,
            .transformOffset = 0,
        };
        accelerationStructureBuildRangeInfos.emplace_back(std::make_unique<VkAccelerationStructureBuildRangeInfoKHR>(accelerationStructureBuildRangeInfo));
        accelerationBuildStructureRangeInfos.push_back(&*accelerationStructureBuildRangeInfos.back());
    }

    // Build the acceleration structure on the device via a one-time command buffer submission
    // Some implementations may support acceleration structure building on the host (VkPhysicalDeviceAccelerationStructureFeaturesKHR->accelerationStructureHostCommands), but we prefer device builds
    device.executeCommandsSync([&](VkCommandBuffer cmdbuf) {
            vk.cmdBuildAccelerationStructuresKHR(
                    cmdbuf,
                    1,
                    &accelerationStructureBuildGeometryInfo,
                    accelerationBuildStructureRangeInfos.data());
            });
    VkAccelerationStructureDeviceAddressInfoKHR accelerationDeviceAddressInfo{};
    accelerationDeviceAddressInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
    accelerationDeviceAddressInfo.accelerationStructure = handle;
    deviceAddress = vk.getAccelerationStructureDeviceAddressKHR(&accelerationDeviceAddressInfo);
}

AccelerationStructure::~AccelerationStructure() {
}

}
