#include "imr/imr.h"

int main() {
    glfwInit();
    glfwWindowHint(GLFW_RESIZABLE, GL_FALSE);
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    auto window = glfwCreateWindow(1024, 1024, "Example", nullptr, nullptr);

    imr::Context context;

    VkSurfaceKHR surface;
    glfwCreateWindowSurface(context.instance, window, nullptr, &surface);

    auto swapchain = vkb::SwapchainBuilder(context.physical_device, context.device, surface).build().value();

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

        vkQueuePresentKHR(context.main_queue, tmp((VkPresentInfoKHR) {
            .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &acquired,
            .swapchainCount = 1,
            .pSwapchains = &swapchain.swapchain,
            .pImageIndices = &image_index,
        }));
    }

    return 0;
}
