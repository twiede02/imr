#include "imr/imr.h"

auto drawDingus(imr::Context& context, VkImage image, VkSemaphore isem) {
    VkCommandBuffer cmdbuf;
    vkAllocateCommandBuffers(context.device, tmp((VkCommandBufferAllocateInfo) {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = context.pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    }), &cmdbuf);

    vkBeginCommandBuffer(cmdbuf, tmp((VkCommandBufferBeginInfo) {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    }));

    VkSemaphore sem;
    vkCreateSemaphore(context.device, tmp((VkSemaphoreCreateInfo) {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
    }), nullptr, &sem);
    vkCmdPipelineBarrier2(cmdbuf, tmp((VkDependencyInfo) {
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
            .newLayout = VK_IMAGE_LAYOUT_GENERAL,
            .image = image,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .levelCount = 1,
                .layerCount = 1,
            }
        }),
    }));

    vkCmdClearColorImage(cmdbuf, image, VK_IMAGE_LAYOUT_GENERAL, tmp((VkClearColorValue) {
        .float32 = { 1.0f, 0.0f, 0.0f, 1.0f},
    }), 1, tmp((VkImageSubresourceRange) {
        .aspectMask = VkImageAspectFlagBits::VK_IMAGE_ASPECT_COLOR_BIT,
        .levelCount = 1,
        .layerCount = 1
    }));

    vkCmdPipelineBarrier2(cmdbuf, tmp((VkDependencyInfo) {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .dependencyFlags = 0,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = tmp((VkImageMemoryBarrier2) {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT,
            .srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
            .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
            .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            .image = image,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .levelCount = 1,
                .layerCount = 1,
            }
        }),
    }));

    vkEndCommandBuffer(cmdbuf);
    vkQueueSubmit(context.main_queue, 1, tmp((VkSubmitInfo) {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &isem,
        .pWaitDstStageMask = tmp((VkPipelineStageFlags) VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT),
        .commandBufferCount = 1,
        .pCommandBuffers = &cmdbuf,
        .signalSemaphoreCount = 1,
        .pSignalSemaphores = &sem,
    }), VK_NULL_HANDLE);

    return sem;
}

int main() {
    glfwInit();
    glfwWindowHint(GLFW_RESIZABLE, GL_FALSE);
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    auto window = glfwCreateWindow(1024, 1024, "Example", nullptr, nullptr);

    imr::Context context;

    VkSurfaceKHR surface;
    glfwCreateWindowSurface(context.instance, window, nullptr, &surface);

    auto swapchain = vkb::SwapchainBuilder(context.physical_device, context.device, surface).add_image_usage_flags(VK_IMAGE_USAGE_TRANSFER_DST_BIT).build().value();

    VkSemaphore prev[32] = { };

    for (;;) {
        VkSemaphore acquired;
        vkCreateSemaphore(context.device, tmp((VkSemaphoreCreateInfo) {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        }), nullptr, &acquired);

        VkFence fence;
        vkCreateFence(context.device, tmp((VkFenceCreateInfo) {
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        }), nullptr, &fence);

        uint32_t image_index;
        vkAcquireNextImageKHR(context.device, swapchain, 0, acquired, fence, &image_index);
        // make sure we 100% are done
        vkWaitForFences(context.device, 1, &fence, true, UINT64_MAX);
        vkDestroyFence(context.device, fence, nullptr);

        if (prev[image_index])
            vkDestroySemaphore(context.device, prev[image_index], nullptr);
        prev[image_index] = acquired;

        auto dingus = drawDingus(context, swapchain.get_images().value()[image_index], acquired);

        vkQueuePresentKHR(context.main_queue, tmp((VkPresentInfoKHR) {
            .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &dingus,
            .swapchainCount = 1,
            .pSwapchains = &swapchain.swapchain,
            .pImageIndices = &image_index,
        }));
    }

    return 0;
}
