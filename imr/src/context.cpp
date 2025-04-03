#include "imr_private.h"

namespace imr {

Context::Context(std::function<void(vkb::InstanceBuilder&)>&& instance_custom,
                 std::function<void(vkb::PhysicalDeviceSelector&)>&& device_custom) {
    _impl = std::make_unique<Impl>();

    auto instance_builder = vkb::InstanceBuilder()
        .use_default_debug_messenger()
        .request_validation_layers()
        .set_minimum_instance_version(1, 3, 0)
        .enable_extension("VK_KHR_get_surface_capabilities2")
        //.enable_extension("VK_EXT_surface_maintenance1")
        .require_api_version(1, 3, 0);

    instance_custom(instance_builder);

    if (auto built = instance_builder
        .build(); built.has_value())
    {
        _impl->vkb_instance = built.value();
        instance = _impl->vkb_instance.instance;
        dispatch_tables.instance = _impl->vkb_instance.make_table();
    } else { throw std::exception(); }

    auto device_selector = vkb::PhysicalDeviceSelector(_impl->vkb_instance)
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
        dispatch_tables.device = _impl->vkb_device.make_table();
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
        .instance = instance,
    }), &_impl->allocator), throw std::exception());
}

Context::~Context() {
    vkDeviceWaitIdle(device);

    vmaDestroyAllocator(_impl->allocator);
    vkDestroyCommandPool(device, pool, nullptr);
    vkb::destroy_device(_impl->vkb_device);
    vkb::destroy_instance(_impl->vkb_instance);
    _impl.reset();
}

}
