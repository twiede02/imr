#include "imr/imr.h"

int main() {
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    auto window = glfwCreateWindow(1024, 1024, "Example", nullptr, nullptr);
    int width, height;
    glfwGetFramebufferSize(window, &width, &height);

    imr::Context context;
    imr::Swapchain swapchain(context, window);
    imr::FpsCounter fps_counter;

    // CPU-side staging buffer
    uint8_t* framebuffer = reinterpret_cast<uint8_t*>(malloc(width * height * 4));

    std::unique_ptr<imr::Buffer> buffer = std::make_unique<imr::Buffer>(context, width * height * 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
    uint8_t* mapped_buffer;
    CHECK_VK(vkMapMemory(context.device, buffer->memory, buffer->memory_offset, buffer->size, 0, (void**) &mapped_buffer), abort());

    VkFence fence;
    vkCreateFence(context.device, tmp((VkFenceCreateInfo) {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT,
    }), nullptr, &fence);

    while (!glfwWindowShouldClose(window)) {
        using Frame = imr::Swapchain::Frame;
        swapchain.beginFrame([&](Frame& frame) {
            int nwidth = frame.width;
            int nheight = frame.height;

            if (nwidth != width || nheight != height) {
                width = nwidth;
                height = nheight;
                free(framebuffer);
                framebuffer = reinterpret_cast<uint8_t*>(malloc(width * height * 4));

                // unmap the old gpu buffer and reallocate it
                vkUnmapMemory(context.device, buffer->memory);
                buffer = std::make_unique<imr::Buffer>(context, width * height * 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
                CHECK_VK(vkMapMemory(context.device, buffer->memory, buffer->memory_offset, buffer->size, 0, (void**) &mapped_buffer), abort());
            }

            vkWaitForFences(context.device, 1, &fence, VK_TRUE, UINT64_MAX);
            CHECK_VK(vkResetFences(context.device, 1, &fence), abort());

            for (size_t i = 0 ; i < width; i++) {
                for (size_t j = 0; j < height; j++) {
                    framebuffer[((j * width) + i) * 4 + 0] = rand() % 255;
                    framebuffer[((j * width) + i) * 4 + 1] = rand() % 255;
                    framebuffer[((j * width) + i) * 4 + 2] = rand() % 255;
                }
            }
            memcpy(mapped_buffer, framebuffer, width * height * 4);
            frame.presentFromBuffer(buffer->handle, fence, std::nullopt);
        });

        fps_counter.tick();
        fps_counter.updateGlfwWindowTitle(window);
        glfwPollEvents();
    }

    vkDeviceWaitIdle(context.device);
    free(framebuffer);

    vkDestroyFence(context.device, fence, nullptr);

    return 0;
}
