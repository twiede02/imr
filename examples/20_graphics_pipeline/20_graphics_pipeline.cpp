#include "imr/imr.h"
#include "imr/util.h"

#include <cmath>
#include "nasl/nasl.h"
#include "nasl/nasl_mat.h"

#include "../common/camera.h"

using namespace nasl;

struct Tri { vec3 v0, v1, v2; vec3 color; };

struct Cube {
    Tri triangles[12];
};

Cube make_cube() {
    /*
     *  +Y
     *  ^
     *  |
     *  |
     *  D------C.
     *  |\     |\
     *  | H----+-G
     *  | |    | |
     *  A-+----B | ---> +X
     *   \|     \|
     *    E------F
     *     \
     *      \
     *       \
     *        v +Z
     *
     * Adapted from
     * https://www.asciiart.eu/art-and-design/geometries
     */
    vec3 A = { 0, 0, 0 };
    vec3 B = { 1, 0, 0 };
    vec3 C = { 1, 1, 0 };
    vec3 D = { 0, 1, 0 };
    vec3 E = { 0, 0, 1 };
    vec3 F = { 1, 0, 1 };
    vec3 G = { 1, 1, 1 };
    vec3 H = { 0, 1, 1 };

    int i = 0;
    Cube cube = {};

    auto add_face = [&](vec3 v0, vec3 v1, vec3 v2, vec3 v3, vec3 color) {
        /*
         * v0 --- v3
         *  |   / |
         *  |  /  |
         *  | /   |
         * v1 --- v2
         */
        cube.triangles[i++] = { v0, v1, v3, color };
        cube.triangles[i++] = { v1, v2, v3, color };
    };

    // top face
    add_face(H, D, C, G, vec3(0, 1, 0));
    // north face
    add_face(A, B, C, D, vec3(1, 0, 0));
    // west face
    add_face(A, D, H, E, vec3(0, 0, 1));
    // east face
    add_face(F, G, C, B, vec3(1, 0, 1));
    // south face
    add_face(E, H, G, F, vec3(0, 1, 1));
    // bottom face
    add_face(E, F, B, A, vec3(1, 1, 0));
    assert(i == 12);
    return cube;
}

struct {
    VkDeviceAddress tri_buffer;
    uint32_t tri_count;
    mat4 matrix;
    float time;
} push_constants_batched;

Camera camera;
CameraFreelookState camera_state = {
    .fly_speed = 1.0f,
    .mouse_sensitivity = 1,
};
CameraInput camera_input;

void camera_update(GLFWwindow*, CameraInput* input);

bool reload_shaders = false;

#define INSTANCES_COUNT 1024

struct Shaders {
    std::vector<std::string> files = { "20_graphics_pipeline.vert.spv", "20_graphics_pipeline.frag.spv" };

    std::vector<std::unique_ptr<imr::ShaderModule>> modules;
    std::vector<std::unique_ptr<imr::ShaderEntryPoint>> entry_points;
    std::unique_ptr<imr::GraphicsPipeline> pipeline;

    Shaders(imr::Device& d, imr::Swapchain& swapchain) {
        imr::GraphicsPipeline::RenderTargetsState rts;
        rts.color.push_back((imr::GraphicsPipeline::RenderTarget) {
            .format = swapchain.format(),
            .blending = {
                .blendEnable = false,
                .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT
            }
        });
        imr::GraphicsPipeline::RenderTarget depth = {
            .format = VK_FORMAT_D32_SFLOAT
        };
        rts.depth = depth;

        imr::GraphicsPipeline::StateBuilder stateBuilder = {
            .vertexInputState = imr::GraphicsPipeline::no_vertex_input(),
            .inputAssemblyState = imr::GraphicsPipeline::simple_triangle_input_assembly(),
            .viewportState = imr::GraphicsPipeline::one_dynamically_sized_viewport(),
            .rasterizationState = imr::GraphicsPipeline::solid_filled_polygons(),
            .multisampleState = imr::GraphicsPipeline::one_spp(),
            .depthStencilState = imr::GraphicsPipeline::simple_depth_testing(),
        };

        std::vector<imr::ShaderEntryPoint*> entry_point_ptrs;
        for (auto filename : files) {
            VkShaderStageFlagBits stage;
            if (filename.ends_with("vert.spv"))
                stage = VK_SHADER_STAGE_VERTEX_BIT;
            else if (filename.ends_with("frag.spv"))
                stage = VK_SHADER_STAGE_FRAGMENT_BIT;
            else
                throw std::runtime_error("Unknown suffix");
            modules.push_back(std::make_unique<imr::ShaderModule>(d, std::move(filename)));
            entry_points.push_back(std::make_unique<imr::ShaderEntryPoint>(*modules.back(), stage, "main"));
            entry_point_ptrs.push_back(entry_points.back().get());
        }
        pipeline = std::make_unique<imr::GraphicsPipeline>(d, std::move(entry_point_ptrs), rts, stateBuilder);
    }
};

int main(int argc, char** argv) {
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    auto window = glfwCreateWindow(1024, 1024, "Example", nullptr, nullptr);

    glfwSetKeyCallback(window, [](GLFWwindow* window, int key, int scancode, int action, int mods) {
        if (key == GLFW_KEY_R && (mods & GLFW_MOD_CONTROL))
            reload_shaders = true;
    });

    imr::Context context;
    imr::Device device(context);
    imr::Swapchain swapchain(device, window);
    imr::FpsCounter fps_counter;

    auto cube = make_cube();

    std::unique_ptr<imr::Buffer> triangles_buffer;
    if (true) {
        triangles_buffer = std::make_unique<imr::Buffer>(device, sizeof(cube.triangles), VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);
        triangles_buffer->uploadDataSync(0, sizeof(cube.triangles), cube.triangles);
    }

    std::unique_ptr<imr::Buffer> matrices_buffer;
    if (true) {
        matrices_buffer = std::make_unique<imr::Buffer>(device, sizeof(nasl::mat4) * INSTANCES_COUNT, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);
    }

    std::vector<vec3> positions;

    for (size_t i = 0; i < INSTANCES_COUNT; i++) {
        vec3 p;
        p.x = ((float)rand() / RAND_MAX) * 20 - 10;
        p.y = ((float)rand() / RAND_MAX) * 20 - 10;
        p.z = ((float)rand() / RAND_MAX) * 20 - 10;
        positions.push_back(p);
    }

    auto prev_frame = imr_get_time_nano();
    float delta = 0;

    camera = {{0, 0, 3}, {0, 0}, 60};

    std::unique_ptr<imr::Image> depthBuffer;

    auto shaders = std::make_unique<Shaders>(device, swapchain);

    auto& vk = device.dispatch;
    while (!glfwWindowShouldClose(window)) {
        fps_counter.tick();
        fps_counter.updateGlfwWindowTitle(window);

        swapchain.renderFrameSimplified([&](imr::Swapchain::SimplifiedRenderContext& context) {
            camera_update(window, &camera_input);
            camera_move_freelook(&camera, &camera_input, &camera_state, delta);

            if (reload_shaders) {
                swapchain.drain();
                shaders = std::make_unique<Shaders>(device, swapchain);
                reload_shaders = false;
            }

            auto& image = context.image();
            auto cmdbuf = context.cmdbuf();

            if (!depthBuffer || depthBuffer->size().width != context.image().size().width || depthBuffer->size().height != context.image().size().height) {
                VkImageUsageFlagBits depthBufferFlags = static_cast<VkImageUsageFlagBits>(VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);
                depthBuffer = std::make_unique<imr::Image>(device, VK_IMAGE_TYPE_2D, context.image().size(), VK_FORMAT_D32_SFLOAT, depthBufferFlags);

                vk.cmdPipelineBarrier2KHR(cmdbuf, tmpPtr((VkDependencyInfo) {
                    .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                    .dependencyFlags = 0,
                    .imageMemoryBarrierCount = 1,
                    .pImageMemoryBarriers = tmpPtr((VkImageMemoryBarrier2) {
                        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                        .srcStageMask = 0,
                        .srcAccessMask = 0,
                        .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                        .dstAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT,
                        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                        .newLayout = VK_IMAGE_LAYOUT_GENERAL,
                        .image = depthBuffer->handle(),
                        .subresourceRange = depthBuffer->whole_image_subresource_range()
                    })
                }));
            }

            vk.cmdClearColorImage(cmdbuf, image.handle(), VK_IMAGE_LAYOUT_GENERAL, tmpPtr((VkClearColorValue) {
                .float32 = { 0.0f, 0.0f, 0.0f, 1.0f },
            }), 1, tmpPtr(image.whole_image_subresource_range()));

            vk.cmdClearDepthStencilImage(cmdbuf, depthBuffer->handle(), VK_IMAGE_LAYOUT_GENERAL, tmpPtr((VkClearDepthStencilValue) {
                .depth = 1.0f,
                .stencil = 0,
            }), 1, tmpPtr(depthBuffer->whole_image_subresource_range()));

            // This barrier ensures that the clear is finished before we run the dispatch.
            // before: all writes from the "transfer" stage (to which the clear command belongs)
            // after: all writes from the "compute" stage
            vk.cmdPipelineBarrier2KHR(cmdbuf, tmpPtr((VkDependencyInfo) {
                .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                .dependencyFlags = 0,
                .memoryBarrierCount = 1,
                .pMemoryBarriers = tmpPtr((VkMemoryBarrier2) {
                    .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
                    .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                    .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
                    .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                    .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                })
            }));

            auto add_render_barrier = [&]() {
                vk.cmdPipelineBarrier2KHR(cmdbuf, tmpPtr((VkDependencyInfo) {
                   .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                   .dependencyFlags = 0,
                   .memoryBarrierCount = 1,
                   .pMemoryBarriers = tmpPtr((VkMemoryBarrier2) {
                       .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
                       .srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                       .srcAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
                       .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                       .dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
                   })
               }));
            };

            // update the push constant data on the host...
            mat4 m = identity_mat4;
            mat4 flip_y = identity_mat4;
            flip_y.rows[1][1] = -1;
            m = m * flip_y;
            mat4 view_mat = camera_get_view_mat4(&camera, context.image().size().width, context.image().size().height);
            m = m * view_mat;
            m = m * translate_mat4(vec3(-0.5, -0.5f, -0.5f));

            auto& pipeline = shaders->pipeline;
            vkCmdBindPipeline(cmdbuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->pipeline());
            //auto shader_bind_helper = pipeline->create_bind_helper();
            //shader_bind_helper->set_storage_image(0, 0, image);
            //shader_bind_helper->set_storage_image(0, 1, *depthBuffer);
            //shader_bind_helper->commit(cmdbuf);

            push_constants_batched.time = ((imr_get_time_nano() / 1000) % 10000000000) / 1000000.0f;
            // the cube data is the same for all
            push_constants_batched.tri_buffer = triangles_buffer->device_address();
            push_constants_batched.tri_count = 12;

            VkImageView color_view;
            vkCreateImageView(device.device, tmpPtr((VkImageViewCreateInfo) {
                .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                .image = image.handle(),
                .viewType = VK_IMAGE_VIEW_TYPE_2D,
                .format = image.format(),
                .subresourceRange = image.whole_image_subresource_range(),
            }), nullptr, &color_view);

            VkImageView depth_view;
            vkCreateImageView(device.device, tmpPtr((VkImageViewCreateInfo) {
                .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                .image = depthBuffer->handle(),
                .viewType = VK_IMAGE_VIEW_TYPE_2D,
                .format = depthBuffer->format(),
                .subresourceRange = depthBuffer->whole_image_subresource_range(),
            }), nullptr, &depth_view);

            vkCmdBeginRendering(cmdbuf, tmpPtr((VkRenderingInfo) {
                .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
                .renderArea = {
                    .extent = {
                        .width = image.size().width,
                        .height = image.size().height,
                    },
                },
                .layerCount = 1,
                .viewMask = 0,
                .colorAttachmentCount = 1,
                .pColorAttachments = tmpPtr((VkRenderingAttachmentInfo) {
                    .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                    .imageView = color_view,
                    .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
                    .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
                    .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                }),
                .pDepthAttachment = tmpPtr((VkRenderingAttachmentInfo) {
                    .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                    .imageView = depth_view,
                    .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
                    .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
                    .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                }),
            }));

            VkViewport viewport {
                    .width = static_cast<float>(image.size().width),
                    .height = static_cast<float>(image.size().height),
                    .maxDepth = 1.0f,
            };
            vkCmdSetViewport(cmdbuf, 0, 1, &viewport);
            VkRect2D scissor = {
                    .extent = {
                            .width = image.size().width,
                            .height = image.size().height,
                    }
            };
            vkCmdSetScissor(cmdbuf, 0, 1, &scissor);

            for (auto pos : positions) {
                //add_render_barrier();

                mat4 cube_matrix = m;
                cube_matrix = cube_matrix * translate_mat4(pos);

                push_constants_batched.matrix = cube_matrix;
                vkCmdPushConstants(cmdbuf, pipeline->layout(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push_constants_batched), &push_constants_batched);
                vkCmdDraw(cmdbuf, 12 * 3, 1, 0, 0);
            }

            vkCmdEndRendering(cmdbuf);

            context.addCleanupAction([=, &device]() {
                //delete shader_bind_helper;
                vkDestroyImageView(device.device, color_view, nullptr);
                vkDestroyImageView(device.device, depth_view, nullptr);
            });

            auto now = imr_get_time_nano();
            delta = ((float) ((now - prev_frame) / 1000L)) / 1000000.0f;
            prev_frame = now;

            glfwPollEvents();
        });
    }

    swapchain.drain();
    return 0;
}
