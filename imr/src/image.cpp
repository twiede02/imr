#include "imr_private.h"

namespace imr {

struct Image::Impl {
    Device& device;
    VmaAllocation vma_allocation;
};

Image::Image(Device& device, VkImageType dim, VkExtent3D size, VkFormat format, VkImageUsageFlagBits usage) : dim(dim), size(size), format(format), usage(usage) {
    _impl = std::make_unique<Impl>(device);
    VkImageCreateInfo image_create_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = dim,
        .format = format,
        .extent = size,
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    VmaAllocationCreateInfo alloc_info = {
        .flags = 0,
        // .usage = VMA_MEMORY_USAGE_AUTO,
    };
    vmaCreateImage(device._impl->allocator, &image_create_info, &alloc_info, &handle, &_impl->vma_allocation, nullptr);
}

Image::~Image() {
    vmaDestroyImage(_impl->device._impl->allocator, handle, _impl->vma_allocation);
}

}
