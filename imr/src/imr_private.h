#include "imr/imr.h"

#include "vk_mem_alloc.h"

namespace imr {

struct Context::Impl {
    vkb::Instance vkb_instance;
};

struct Device::Impl {
    vkb::PhysicalDevice vkb_physical_device;
    vkb::Device vkb_device;

    VmaAllocator allocator;

    //std::vector<std::unique_ptr<Buffer>> buffers;
    std::vector<std::unique_ptr<Image>> images;
};

}