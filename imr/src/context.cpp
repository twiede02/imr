#include "imr_private.h"

namespace imr {

Context::Context(std::function<void(vkb::InstanceBuilder&)>&& instance_custom) {
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
        dispatch = _impl->vkb_instance.make_table();
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
}

Context::~Context() {
    vkb::destroy_instance(_impl->vkb_instance);
    _impl.reset();
}

}
