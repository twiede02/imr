#include "hag/hag.h"

#include "VkBootstrap.h"

int main()
{
    auto instance = vkb::InstanceBuilder()
      .use_default_debug_messenger()
      .request_validation_layers()
      .set_minimum_instance_version(1, 2, 0)
    .build().value();
    auto dispatch = instance.make_table();

    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    auto window = glfwCreateWindow(1024, 768, "HAG", nullptr, nullptr);

    VkSurfaceKHR surface;
    glfwCreateWindowSurface(instance, window, nullptr, &surface);

    auto phys_device = vkb::PhysicalDeviceSelector(instance).add_required_extension("VK_KHR_dynamic_rendering").set_surface(surface).select().value();
    auto device = vkb::DeviceBuilder(phys_device).build().value();

    auto swapchain = vkb::SwapchainBuilder(device).build().value();
    //auto queue = device.get_queue(vkb::QueueType::graphics | vkb::QueueType::present);
    auto pqueue = device.get_queue(vkb::QueueType::present);

    auto fb = 

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        swapchain
    }

    return 0;
}
