#include "hag/hag.h"

#include "VkBootstrap.h"

#include <ctime>
#include <memory>

static uint64_t shd_get_time_nano() {
    struct timespec t;
    timespec_get(&t, TIME_UTC);
    return t.tv_sec * 1000000000 + t.tv_nsec;
}

int main() {
    hag::Context context;
    auto& vk = context.dispatch_tables.device;

    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    auto window = glfwCreateWindow(1024, 768, "HAG", nullptr, nullptr);
    hag::Swapchain swapchain(context, window);

    VkFence last_fence;
    vkCreateFence(context.device, tmp((VkFenceCreateInfo) {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT,
    }), nullptr, &last_fence);

    VkFence next_fence;
    vkCreateFence(context.device, tmp((VkFenceCreateInfo) {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = 0,
    }), nullptr, &next_fence);

    uint64_t last_epoch = shd_get_time_nano();
    int frames_since_last_epoch = 0;

    while (!glfwWindowShouldClose(window)) {
        uint64_t now = shd_get_time_nano();
        uint64_t delta = now - last_epoch;
        if (delta > 1000000000 /* 1 second */) {
            last_epoch = now;
            if (frames_since_last_epoch > 0) {
                int fps = frames_since_last_epoch;
                float avg_frametime = (delta / 1000000.0f /* scale to ms */) / frames_since_last_epoch;
                std::string str = "Fps: ";
                str.append(std::to_string(fps));
                str.append(", Avg frametime: ");
                str.append(std::to_string(avg_frametime));
                str.append("ms");
                glfwSetWindowTitle(window, str.c_str());
            }
            frames_since_last_epoch = 0;
        }

        vkWaitForFences(context.device, 1, &last_fence, VK_TRUE, UINT64_MAX);

        VkExtent3D extents = { 1024, 768, 1};
        auto image = new hag::Image (context, VK_IMAGE_TYPE_2D, extents, VK_FORMAT_R8G8B8A8_UNORM, (VkImageUsageFlagBits) (VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT));

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
        vk.cmdPipelineBarrier2KHR(cmdbuf, tmp((VkDependencyInfo) {
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
                .image = image->handle,
                .subresourceRange = {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .levelCount = 1,
                    .layerCount = 1,
                }
            }),
        }));

        vk.cmdClearColorImage(cmdbuf, image->handle, VK_IMAGE_LAYOUT_GENERAL, tmp((VkClearColorValue) {
                .float32 = { 1.0f, 1.0f, 0.0f, 1.0f},
            }), 1, tmp((VkImageSubresourceRange) {
                .aspectMask = VkImageAspectFlagBits::VK_IMAGE_ASPECT_COLOR_BIT,
                .levelCount = 1,
                .layerCount = 1
            }));
        vkEndCommandBuffer(cmdbuf);
        vk.queueSubmit(context.main_queue, 1, tmp((VkSubmitInfo) {
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .waitSemaphoreCount = 0,
            .commandBufferCount = 1,
            .pCommandBuffers = &cmdbuf,
            .signalSemaphoreCount = 1,
            .pSignalSemaphores = &sem,
        }), VK_NULL_HANDLE);

        // vkTransitionImageLayoutEXT(context.device, 1, tmp((VkHostImageLayoutTransitionInfoEXT) {
        //
        // }))

        swapchain.add_to_delete_queue([=, &context]() {
            delete image;
            vkDestroySemaphore(context.device, sem, nullptr);
        });
        swapchain.present(image->handle, next_fence, { sem }, VK_IMAGE_LAYOUT_GENERAL, std::make_optional<VkExtent2D>(image->size.width, image->size.height));

        frames_since_last_epoch++;
        std::swap(last_fence, next_fence);
        glfwPollEvents();
    }

    vkDeviceWaitIdle(context.device);

    vkDestroyFence(context.device, next_fence, nullptr);
    vkDestroyFence(context.device, last_fence, nullptr);

    return 0;
}
