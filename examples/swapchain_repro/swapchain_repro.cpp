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

    VkSurfaceKHR surface;
    glfwCreateWindowSurface(context.instance, window, nullptr, &surface);

    VkSemaphore acquired;
    vkCreateSemaphore(context.device, tmp((VkSemaphoreCreateInfo) {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
    }), nullptr, &acquired);

    uint32_t image_index;
    auto swapchain = vkb::SwapchainBuilder(context.physical_device, context.device, surface).build().value();
    vkAcquireNextImageKHR(context.device, swapchain, 0, acquired, VK_NULL_HANDLE, &image_index);

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
    vkEndCommandBuffer(cmdbuf);
    vkQueueSubmit(context.main_queue, 1, tmp((VkSubmitInfo) {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &acquired,
        .pWaitDstStageMask = tmp((VkPipelineStageFlags) VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT),
        .commandBufferCount = 1,
        .pCommandBuffers = &cmdbuf,
        .signalSemaphoreCount = 0,
    }), VK_NULL_HANDLE);

    return 0;
}
