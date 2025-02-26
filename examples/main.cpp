#include "hag/hag.h"

#include "VkBootstrap.h"

#include <ctime>
#include <memory>

static uint64_t shd_get_time_nano() {
    struct timespec t;
    timespec_get(&t, TIME_UTC);
    return t.tv_sec * 1000000000 + t.tv_nsec;
}

int main()
{
    hag::Context context;

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
        auto image = new hag::Image (context, VK_IMAGE_TYPE_2D, extents, VK_FORMAT_R8G8B8A8_UNORM, (VkImageUsageFlagBits) (VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT));

        swapchain.add_to_delete_queue([=]() {
            delete image;
        });
        swapchain.present(image->handle, next_fence);

        frames_since_last_epoch++;
        std::swap(last_fence, next_fence);
        glfwPollEvents();
    }

    vkDeviceWaitIdle(context.device);

    vkDestroyFence(context.device, next_fence, nullptr);
    vkDestroyFence(context.device, last_fence, nullptr);

    return 0;
}
