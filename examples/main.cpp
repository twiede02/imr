#include "hag/hag.h"

#include "VkBootstrap.h"

auto tmp(auto&& t) { return &t; }

int main()
{
    auto instance = vkb::InstanceBuilder()
      .use_default_debug_messenger()
      .request_validation_layers()
      .set_minimum_instance_version(1, 2, 0)
    .build().value();
    auto instance_dispatch = instance.make_table();

    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    auto window = glfwCreateWindow(1024, 768, "HAG", nullptr, nullptr);

    VkSurfaceKHR surface;
    glfwCreateWindowSurface(instance, window, nullptr, &surface);

    auto phys_device = vkb::PhysicalDeviceSelector(instance)
        .add_required_extension("VK_KHR_multiview")
        .add_required_extension("VK_KHR_maintenance2")
        .add_required_extension("VK_KHR_create_renderpass2")
        .add_required_extension("VK_KHR_depth_stencil_resolve")
        .add_required_extension("VK_KHR_dynamic_rendering")
        .add_required_extension("VK_KHR_synchronization2")
        .set_surface(surface)
        .add_required_extension_features((VkPhysicalDeviceSynchronization2FeaturesKHR) {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES_KHR,
            .synchronization2 = true,
        })
    .select().value();
    auto device = vkb::DeviceBuilder(phys_device).build().value();
    auto device_dispatch = device.make_table();

    auto swapchain = vkb::SwapchainBuilder(device).add_image_usage_flags(VK_IMAGE_USAGE_TRANSFER_DST_BIT).build().value();
    auto queue_index = device.get_queue_index(vkb::QueueType((int) vkb::QueueType::graphics | (int) vkb::QueueType::present)).value();
    auto queue = device.get_queue(vkb::QueueType((int) vkb::QueueType::graphics | (int) vkb::QueueType::present)).value();
    //auto pqueue = device.get_queue(vkb::QueueType::present);

    VkCommandPool pool;
    vkCreateCommandPool(device.device, tmp((VkCommandPoolCreateInfo) {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .queueFamilyIndex = queue_index,
    }), nullptr, &pool);

    VkFence last_fence;
    vkCreateFence(device.device, tmp((VkFenceCreateInfo) {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT,
    }), nullptr, &last_fence);

    VkFence next_fence;
    vkCreateFence(device.device, tmp((VkFenceCreateInfo) {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = 0,
    }), nullptr, &next_fence);

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        VkSemaphore image_acquired;
        vkCreateSemaphore(device.device, tmp((VkSemaphoreCreateInfo) {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        }), nullptr, &image_acquired);

        VkSemaphore ready2present;
        vkCreateSemaphore(device.device, tmp((VkSemaphoreCreateInfo) {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        }), nullptr, &ready2present);

        vkWaitForFences(device.device, 1, &last_fence, VK_TRUE, UINT64_MAX);
        uint32_t image_index;
        device_dispatch.acquireNextImageKHR(swapchain, UINT64_MAX, image_acquired, VK_NULL_HANDLE, &image_index);

        VkCommandBuffer cmdbuf;
        vkAllocateCommandBuffers(device.device, tmp((VkCommandBufferAllocateInfo) {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = pool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1,
        }), &cmdbuf);

        vkBeginCommandBuffer(cmdbuf, tmp((VkCommandBufferBeginInfo) {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        }));

        device_dispatch.cmdPipelineBarrier2KHR(cmdbuf, tmp((VkDependencyInfo) {
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .dependencyFlags = 0,
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers = tmp((VkImageMemoryBarrier2) {
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                .srcStageMask = VK_PIPELINE_STAGE_2_NONE,
                .srcAccessMask = VK_ACCESS_2_NONE,
                .dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                .dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                .image = swapchain.get_images().value()[image_index],
                .subresourceRange = {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .levelCount = 1,
                    .layerCount = 1,
                }
            }),
        }));
        vkCmdClearColorImage(cmdbuf, swapchain.get_images().value()[image_index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, tmp((VkClearColorValue) {
            .float32 = { 1.0f, 0.0f, 0.0f, 1.0f},
        }), 1, tmp((VkImageSubresourceRange) {
            .aspectMask = VkImageAspectFlagBits::VK_IMAGE_ASPECT_COLOR_BIT,
            .levelCount = 1,
            .layerCount = 1
        }));
        device_dispatch.cmdPipelineBarrier2KHR(cmdbuf, tmp((VkDependencyInfo) {
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .dependencyFlags = 0,
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers = tmp((VkImageMemoryBarrier2) {
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
                .dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                .dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                .image = swapchain.get_images().value()[image_index],
                .subresourceRange = {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .levelCount = 1,
                    .layerCount = 1,
                }
            }),
        }));

        vkEndCommandBuffer(cmdbuf);
        vkResetFences(device.device, 1, &next_fence);
        vkQueueSubmit(queue, 1, tmp((VkSubmitInfo) {
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &image_acquired,
            .pWaitDstStageMask = tmp(VkPipelineStageFlags(VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT)),
            .commandBufferCount = 1,
            .pCommandBuffers = &cmdbuf,
            .signalSemaphoreCount = 1,
            .pSignalSemaphores = &ready2present,
        }), next_fence);

        /*vkWaitSemaphores(device.device, tmp((VkSemaphoreWaitInfo) {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
            .semaphoreCount = 1,
            .pSemaphores = &ready2present,
        }), UINT64_MAX);*/
        device_dispatch.queuePresentKHR(queue, tmp((VkPresentInfoKHR) {
            .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &ready2present,
            .swapchainCount = 1,
            .pSwapchains = &swapchain.swapchain,
            .pImageIndices = tmp(image_index),
        }));

        std::swap(last_fence, next_fence);
    }

    return 0;
}
