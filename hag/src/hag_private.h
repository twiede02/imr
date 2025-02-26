#include "hag/hag.h"

#include "vk_mem_alloc.h"

namespace hag {

struct Context::Impl {
    vkb::Instance vkb_instance;
    vkb::PhysicalDevice vkb_physical_device;
    vkb::Device vkb_device;

    VmaAllocator allocator;

    //std::vector<std::unique_ptr<Buffer>> buffers;
    std::vector<std::unique_ptr<Image>> images;
};

}