#include "imr_private.h"

namespace imr {

struct Buffer::Impl {
    Context& context;
    VmaAllocation allocation;
    VmaAllocationInfo allocation_info;
};

Buffer::Buffer(imr::Context& context, size_t size, VkBufferUsageFlags usage, VkMemoryPropertyFlags memory_property) : size(size) {
    _impl = std::make_unique<Impl>(context);
    VkBufferCreateInfo buffer_ci = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .flags = 0,
        .size = size,
        .usage = usage | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    VmaAllocationCreateInfo vma_aci = {
        .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT,
        .usage = VMA_MEMORY_USAGE_UNKNOWN,
        .requiredFlags = memory_property
    };
    CHECK_VK(vmaCreateBuffer(context._impl->allocator, &buffer_ci, &vma_aci, &handle, &_impl->allocation, &_impl->allocation_info), throw std::exception());
    device_address = vkGetBufferDeviceAddress(context.device, tmp((VkBufferDeviceAddressInfo) {
        .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
        .buffer = handle,
    }));
    memory = _impl->allocation_info.deviceMemory;
    memory_offset = _impl->allocation_info.offset;
}

Buffer::~Buffer() {
    vmaDestroyBuffer(_impl->context._impl->allocator, handle, _impl->allocation);
}

}
