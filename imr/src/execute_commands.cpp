#include "imr_private.h"

namespace imr {

void Device::executeCommandsSync(std::function<void(VkCommandBuffer)> lambda) {
    VkCommandBuffer cmdbuf;
    vkAllocateCommandBuffers(device.device, tmpPtr((VkCommandBufferAllocateInfo) {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    }), &cmdbuf);
    vkBeginCommandBuffer(cmdbuf, tmpPtr((VkCommandBufferBeginInfo) {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    }));

    lambda(cmdbuf);

    VkFence fence;
    vkCreateFence(device.device, tmpPtr((VkFenceCreateInfo) {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = 0,
    }), nullptr, &fence);

    vkEndCommandBuffer(cmdbuf);
    vkQueueSubmit(main_queue, 1, tmpPtr((VkSubmitInfo) {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount = 0,
        .pWaitSemaphores = nullptr,
        .pWaitDstStageMask = tmpPtr((VkPipelineStageFlags) VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT),
        .commandBufferCount = 1,
        .pCommandBuffers = &cmdbuf,
        .signalSemaphoreCount = 0,
        .pSignalSemaphores = nullptr,
    }), fence);

    vkWaitForFences(device, 1, &fence, true, UINT64_MAX);

    vkDestroyFence(device.device, fence, nullptr);
    vkFreeCommandBuffers(device.device, pool, 1, &cmdbuf);
}


}