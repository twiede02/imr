#include "imr/imr.h"
#include "imr/util.h"

int main() {
    glfwInit();
    glfwWindowHint(GLFW_RESIZABLE, GL_FALSE);
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    auto window = glfwCreateWindow(1024, 1024, "Example", nullptr, nullptr);
    int width, height;
    glfwGetFramebufferSize(window, &width, &height);

    imr::Context context;
    imr::Swapchain swapchain(context, window);
    imr::FpsCounter fps_counter;

    auto& vk = context.dispatch_tables.device;

    while (!glfwWindowShouldClose(window)) {
        swapchain.beginFrame([&](auto& frame) {
            auto& image = frame.swapchain_image;

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
            CHECK_VK(vkCreateSemaphore(context.device, tmp((VkSemaphoreCreateInfo) {
                .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
            }), nullptr, &sem), abort());

            vk.setDebugUtilsObjectNameEXT(tmp((VkDebugUtilsObjectNameInfoEXT) {
                .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
                .objectType = VK_OBJECT_TYPE_SEMAPHORE,
                .objectHandle = reinterpret_cast<uint64_t>(sem),
                .pObjectName = (std::string("11_present_image.sem") + std::to_string(frame.id)).c_str(),
            }));

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

            vk.cmdPipelineBarrier2KHR(cmdbuf, tmp((VkDependencyInfo) {
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

            VkFence fence;
            vkCreateFence(context.device, tmp((VkFenceCreateInfo) {
                .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
                .flags = 0,
            }), nullptr, &fence);

            vkEndCommandBuffer(cmdbuf);
            vkQueueSubmit(context.main_queue, 1, tmp((VkSubmitInfo) {
                .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                .waitSemaphoreCount = 1,
                .pWaitSemaphores = &frame.swapchain_image_available,
                .pWaitDstStageMask = tmp((VkPipelineStageFlags) VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT),
                .commandBufferCount = 1,
                .pCommandBuffers = &cmdbuf,
                .signalSemaphoreCount = 1,
                .pSignalSemaphores = &sem,
            }), fence);

            printf("Frame submitted with fence = %llx\n", fence);

            frame.add_to_delete_queue(fence, [=, &context]() {
                //vkWaitForFences(context.device, 1, &fence, true, UINT64_MAX);
                vkDestroyFence(context.device, fence, nullptr);
                vkDestroySemaphore(context.device, sem, nullptr);
                vkFreeCommandBuffers(context.device, context.pool, 1, &cmdbuf);
            });
            frame.present(sem);
        });

        fps_counter.tick();
        fps_counter.updateGlfwWindowTitle(window);
        glfwPollEvents();
    }

    vkDeviceWaitIdle(context.device);

    return 0;
}
