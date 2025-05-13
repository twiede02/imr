#include "imr_private.h"

namespace imr {

struct Image::Impl {
    Device& device;
    VkImage handle;
    VkImageType dim;
    VkExtent3D size;
    VkFormat format;
    std::optional<VmaAllocation> vma_allocation;

    Impl(Device& device, VkImageType dim, VkExtent3D size, VkFormat format)
    : device(device), handle(VK_NULL_HANDLE), dim(dim), size(size), format(format) {}
    Impl(Device& device, VkImage existing_handle, VkImageType dim, VkExtent3D size, VkFormat format)
    : device(device), handle(existing_handle), dim(dim), size(size), format(format) {}
};

VkImage Image::handle() const { return _impl->handle; }
VkImageType Image::dim() const { return _impl->dim; }
VkExtent3D Image::size() const { return _impl->size; }
VkFormat Image::format() const { return _impl->format; }

Image::Image(Device& device, VkImageType dim, VkExtent3D size, VkFormat format, VkImageUsageFlagBits usage) {
    _impl = std::make_unique<Impl>(device, dim, size, format);
    VkImageCreateInfo image_create_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = dim,
        .format = format,
        .extent = size,
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = (VkImageUsageFlags) usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    VmaAllocationCreateInfo alloc_info = {
        .flags = 0,
        // .usage = VMA_MEMORY_USAGE_AUTO,
    };
    VmaAllocation& vma_allocation = _impl->vma_allocation.emplace();
    vmaCreateImage(device._impl->allocator, &image_create_info, &alloc_info, &_impl->handle, &vma_allocation, nullptr);
}

Image make_image_from(Device& device, VkImage existing_handle, VkImageType dim, VkExtent3D size, VkFormat format) {
    return Image(Image::Impl(device, existing_handle, dim, size, format));
}

Image::Image(Impl&& impl) {
    _impl = std::make_unique<Impl>(impl);
}

Image::Image(Image&& other) : _impl(std::move(other._impl)) {}

Image::~Image() {
    if (_impl)
        if (_impl->vma_allocation)
            vmaDestroyImage(_impl->device._impl->allocator, _impl->handle, _impl->vma_allocation.value());
}

}
