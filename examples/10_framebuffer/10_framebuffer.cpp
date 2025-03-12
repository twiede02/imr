#include "imr/imr.h"
#include "imr/util.h"

#include "VkBootstrap.h"

#include <filesystem>

int main() {
    imr::Context context;
    auto& vk = context.dispatch_tables.device;

    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    auto window = glfwCreateWindow(1024, 1024, "Example", nullptr, nullptr);
    imr::Swapchain swapchain(context, window);

    uint64_t last_epoch = shd_get_time_nano();
    int frames_since_last_epoch = 0;

    int width, height;
    glfwGetWindowSize(window, &width, &height);
    uint8_t* framebuffer = reinterpret_cast<uint8_t*>(malloc(width * height * 4));
    for (size_t i = 0 ; i < width; i++) {
        for (size_t j = 0; j < height; j++) {
            framebuffer[((j * width) + i) * 4 + 0] = 0;
            framebuffer[((j * width) + i) * 4 + 1] = 255;
            framebuffer[((j * width) + i) * 4 + 2] = 255;
        }
    }

    imr::Buffer buffer = imr::Buffer(context, width * height * 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
    uint8_t* mapped_buffer;
    CHECK_VK(vkMapMemory(context.device, buffer.memory, buffer.memory_offset, buffer.size, 0, (void**) &mapped_buffer), abort());
    memcpy(mapped_buffer, framebuffer, width * height * 4);

    VkFence fence;
    vkCreateFence(context.device, tmp((VkFenceCreateInfo) {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT,
    }), nullptr, &fence);

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

        vkWaitForFences(context.device, 1, &fence, VK_TRUE, UINT64_MAX);
        swapchain.presentFromBuffer(buffer.handle, fence, std::nullopt);

        frames_since_last_epoch++;
        glfwPollEvents();
    }

    vkDeviceWaitIdle(context.device);

    vkDestroyFence(context.device, fence, nullptr);

    return 0;
}
