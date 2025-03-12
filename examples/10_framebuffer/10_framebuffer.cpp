#include "imr/imr.h"

#include "VkBootstrap.h"

#include <filesystem>

int main() {
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    auto window = glfwCreateWindow(1024, 1024, "Example", nullptr, nullptr);

    imr::Context context;
    imr::Swapchain swapchain(context, window);
    imr::FpsCounter fps_counter;

    int width, height;
    glfwGetWindowSize(window, &width, &height);
    uint8_t* framebuffer = reinterpret_cast<uint8_t*>(malloc(width * height * 4));

    imr::Buffer buffer = imr::Buffer(context, width * height * 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
    uint8_t* mapped_buffer;
    CHECK_VK(vkMapMemory(context.device, buffer.memory, buffer.memory_offset, buffer.size, 0, (void**) &mapped_buffer), abort());

    VkFence fence;
    vkCreateFence(context.device, tmp((VkFenceCreateInfo) {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT,
    }), nullptr, &fence);

    while (!glfwWindowShouldClose(window)) {
        fps_counter.tick();
        fps_counter.updateGlfwWindowTitle(window);

        vkWaitForFences(context.device, 1, &fence, VK_TRUE, UINT64_MAX);

        /*for (size_t i = 0 ; i < width; i++) {
            for (size_t j = 0; j < height; j++) {
                mapped_buffer[((j * width) + i) * 4 + 0] = rand() % 255;
                mapped_buffer[((j * width) + i) * 4 + 1] = rand() % 255;
                mapped_buffer[((j * width) + i) * 4 + 2] = rand() % 255;
            }
        }*/
        for (size_t i = 0 ; i < width; i++) {
            for (size_t j = 0; j < height; j++) {
                framebuffer[((j * width) + i) * 4 + 0] = rand() % 255;
                framebuffer[((j * width) + i) * 4 + 1] = rand() % 255;
                framebuffer[((j * width) + i) * 4 + 2] = rand() % 255;
            }
        }
        memcpy(mapped_buffer, framebuffer, width * height * 4);

        swapchain.presentFromBuffer(buffer.handle, fence, std::nullopt);

        glfwPollEvents();
    }

    vkDeviceWaitIdle(context.device);

    vkDestroyFence(context.device, fence, nullptr);

    return 0;
}
