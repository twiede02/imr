#include "imr_private.h"

namespace imr {

Device::Device(Context& context,
               std::function<void(vkb::PhysicalDeviceSelector&)>&& device_custom) : context(context) {
    _impl = std::make_unique<Impl>();

    auto device_selector = vkb::PhysicalDeviceSelector(context._impl->vkb_instance)
        .add_required_extension("VK_KHR_maintenance2")
        .add_required_extension("VK_KHR_create_renderpass2")
        .add_required_extension("VK_KHR_dynamic_rendering")
        .add_required_extension("VK_KHR_synchronization2")
        .set_minimum_version(1, 2)
        .set_required_features((VkPhysicalDeviceFeatures) {
            .shaderUniformBufferArrayDynamicIndexing = true,
        })
        .set_required_features_11((VkPhysicalDeviceVulkan11Features) {
        })
        .set_required_features_12((VkPhysicalDeviceVulkan12Features) {
            .scalarBlockLayout = true,
            .bufferDeviceAddress = true,
        })
            // .set_surface(surface)
        .defer_surface_initialization()
        .add_required_extension_features((VkPhysicalDeviceSynchronization2FeaturesKHR) {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES_KHR,
            .synchronization2 = true,
        });

    device_custom(device_selector);

    if (auto built = device_selector.select(); built.has_value())
    {
        _impl->vkb_physical_device = built.value();
        physical_device = _impl->vkb_physical_device.physical_device;
    }

    if (auto built = vkb::DeviceBuilder(_impl->vkb_physical_device)
            .build(); built.has_value())
    {
        _impl->vkb_device = built.value();
        device = _impl->vkb_device;
        dispatch = _impl->vkb_device.make_table();
    }

    main_queue_idx = _impl->vkb_device.get_queue_index(vkb::QueueType((int) vkb::QueueType::graphics | (int) vkb::QueueType::present)).value();
    main_queue = _impl->vkb_device.get_queue(vkb::QueueType((int) vkb::QueueType::graphics | (int) vkb::QueueType::present)).value();

    CHECK_VK(vkCreateCommandPool(device, tmp((VkCommandPoolCreateInfo) {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .queueFamilyIndex = main_queue_idx,
    }), nullptr, &pool), throw std::exception());

    CHECK_VK(vmaCreateAllocator(tmp((VmaAllocatorCreateInfo) {
        .flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT,
        .physicalDevice = physical_device,
        .device = device,
        .instance = context.instance,
    }), &_impl->allocator), throw std::exception());
}

Device::~Device() {
    vkDeviceWaitIdle(device);

    vmaDestroyAllocator(_impl->allocator);
    vkDestroyCommandPool(device, pool, nullptr);
    vkb::destroy_device(_impl->vkb_device);
    _impl.reset();
}

}
