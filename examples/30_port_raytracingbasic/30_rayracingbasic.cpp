#include "imr/imr.h"
#include "imr/util.h"

#include "VkBootstrap.h"

#include <ctime>
#include <memory>
#include <filesystem>
#include <cmath>
#include <iostream>
#include "nasl/nasl.h"
#include "nasl/nasl_mat.h"
#include "../common/camera.h"

#define VK_CHECK_RESULT(do) CHECK_VK((do), throw std::runtime_error(#do))

void camera_update(GLFWwindow*, CameraInput* input);

using namespace nasl;

struct Face { vec3 v0, v1, v2, v3; };

struct Tri { uint32_t i0, i1, i2; };

struct Cube {
    Face faces[6];
    Tri tris[6][2];
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
        cube.faces[i] = { v0, v1, v2, v3 };
        /*
         * v0 --- v3
         *  |   / |
         *  |  /  |
         *  | /   |
         * v1 --- v2
         */
        uint32_t bi = i * 4;
        cube.tris[i][0] = { bi + 0, bi + 1, bi + 3 };
        cube.tris[i][1] = { bi + 1, bi + 2, bi + 3 };
        i++;
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
    assert(i == 6);
    return cube;
}

class VulkanExample
{
public:
    GLFWwindow* window;
    
    imr::Context context;
    std::unique_ptr<imr::Device> device;
    std::unique_ptr<imr::Swapchain> swapchain;

    Camera camera {
        .position = {0, 0, -2.5f},
        .rotation = {0},
        .fov = 60.0,
    };
    CameraFreelookState camera_state = {
        .fly_speed = 1.0f,
        .mouse_sensitivity = 1,
    };
    CameraInput camera_input;
    bool resized = false;
    bool viewUpdated = false;
    uint16_t width = 1024;
    uint16_t height = 1024;
    
    VkPhysicalDeviceRayTracingPipelinePropertiesKHR  rayTracingPipelineProperties{};
    //VkPhysicalDeviceAccelerationStructureFeaturesKHR accelerationStructureFeatures{};

    VkPhysicalDeviceBufferDeviceAddressFeatures enabledBufferDeviceAddresFeatures{};
    VkPhysicalDeviceRayTracingPipelineFeaturesKHR enabledRayTracingPipelineFeatures{};
    VkPhysicalDeviceAccelerationStructureFeaturesKHR enabledAccelerationStructureFeatures{};

    std::unique_ptr<imr::AccelerationStructure> bottomLevelAS;
    std::unique_ptr<imr::AccelerationStructure> topLevelAS;

    std::unique_ptr<imr::RayTracingPipeline> imr_pipeline;

    std::unique_ptr<imr::Buffer> vertexBuffer;
    std::unique_ptr<imr::Buffer> indexBuffer;
    uint32_t indexCount;

    struct Settings {
        /** @brief Activates validation layers (and message output) when set to true */
        bool validation = false;
        /** @brief Set to true if fullscreen mode has been requested via command line */
        bool fullscreen = false;
        /** @brief Set to true if v-sync will be forced for the swapchain */
        bool vsync = false;
        /** @brief Enable UI overlay */
        bool overlay = true;
    } settings;

    struct UniformData {
        mat4 viewInverse;
        mat4 projInverse;
    } uniformData;

    std::unique_ptr<imr::Buffer> ubo;
    std::unique_ptr<imr::Image> storage_image;

    VulkanExample() {
        glfwInit();
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        window = glfwCreateWindow(width, height, "Example", nullptr, nullptr);

        //glfwSetKeyCallback(window, [](GLFWwindow* window, int key, int scancode, int action, int mods) {
        //  if (key == GLFW_KEY_R && (mods & GLFW_MOD_CONTROL))
        //      reload_shaders = true;
        //});

        device = std::make_unique<imr::Device>(context, [&](vkb::PhysicalDeviceSelector& selector) {
            selector.add_required_extension(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
            selector.add_required_extension(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME);

            // Required by VK_KHR_acceleration_structure
            selector.add_required_extension(VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME);
            selector.add_required_extension(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
            selector.add_required_extension(VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME);

            // Required for VK_KHR_ray_tracing_pipeline
            selector.add_required_extension(VK_KHR_SPIRV_1_4_EXTENSION_NAME);

            // Required by VK_KHR_spirv_1_4
            selector.add_required_extension(VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME);

            enabledBufferDeviceAddresFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES;
            enabledBufferDeviceAddresFeatures.bufferDeviceAddress = VK_TRUE;
            selector.add_required_extension_features(enabledBufferDeviceAddresFeatures);

            enabledRayTracingPipelineFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR;
            enabledRayTracingPipelineFeatures.rayTracingPipeline = VK_TRUE;
            selector.add_required_extension_features(enabledRayTracingPipelineFeatures);

            enabledAccelerationStructureFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
            enabledAccelerationStructureFeatures.accelerationStructure = VK_TRUE;
            //enabledAccelerationStructureFeatures.accelerationStructureHostCommands = VK_TRUE;
            selector.add_required_extension_features(enabledAccelerationStructureFeatures);
        });
        
        swapchain = std::make_unique<imr::Swapchain>(*device, window);
        imr::FpsCounter fps_counter;

        ubo = std::make_unique<imr::Buffer>(*device, sizeof(uniformData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
        storage_image = std::make_unique<imr::Image>(*device, VK_IMAGE_TYPE_2D, (VkExtent3D) {width, height, 1}, swapchain->format(), (VkImageUsageFlagBits) (VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT));

        prepare();
    }

    void draw(imr::Swapchain::SimplifiedRenderContext& context) {
        auto& device = *this->device;
        auto& vk = device.dispatch;

        auto& image = context.image();
        auto cmdbuf = context.cmdbuf();

        /*
            Setup the buffer regions pointing to the shaders in our shader binding table
        */

        const uint32_t handleSizeAligned = (rayTracingPipelineProperties.shaderGroupHandleSize + rayTracingPipelineProperties.shaderGroupHandleAlignment - 1) & ~(rayTracingPipelineProperties.shaderGroupHandleAlignment - 1);

        VkStridedDeviceAddressRegionKHR raygenShaderSbtEntry{};
        raygenShaderSbtEntry.deviceAddress = imr_pipeline->raygenShaderBindingTable()->device_address();
        raygenShaderSbtEntry.stride = handleSizeAligned;
        raygenShaderSbtEntry.size = handleSizeAligned;

        VkStridedDeviceAddressRegionKHR missShaderSbtEntry{};
        missShaderSbtEntry.deviceAddress = imr_pipeline->missShaderBindingTable()->device_address();
        missShaderSbtEntry.stride = handleSizeAligned;
        missShaderSbtEntry.size = handleSizeAligned;

        VkStridedDeviceAddressRegionKHR hitShaderSbtEntry{};
        hitShaderSbtEntry.deviceAddress = imr_pipeline->hitShaderBindingTable()->device_address();
        hitShaderSbtEntry.stride = handleSizeAligned;
        hitShaderSbtEntry.size = handleSizeAligned;

        VkStridedDeviceAddressRegionKHR callableShaderSbtEntry{};

        /*
            Dispatch the ray tracing commands
        */
        vkCmdBindPipeline(cmdbuf, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, imr_pipeline->pipeline());

        auto bind_helper = imr_pipeline->create_bind_helper();
        bind_helper->set_acceleration_structure(0, 0, *topLevelAS);
        bind_helper->set_storage_image(0, 1, *storage_image);
        bind_helper->set_uniform_buffer(0, 2, *ubo);
        bind_helper->commit(cmdbuf);

        context.addCleanupAction([=, &device]() {
            delete bind_helper;
        });

        auto setImageLayout = [&](imr::Image& image, VkImageLayout new_layout, VkImageLayout old_layout = VK_IMAGE_LAYOUT_UNDEFINED) {
            vkCmdPipelineBarrier2(cmdbuf, tmpPtr((VkDependencyInfo) {
                .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                .dependencyFlags = 0,
                .imageMemoryBarrierCount = 1,
                .pImageMemoryBarriers = tmpPtr((VkImageMemoryBarrier2) {
                    .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                    .srcStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                    .srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT,
                    .dstStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                    .dstAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT,
                    .oldLayout = old_layout,
                    .newLayout = new_layout,
                    .image = image.handle(),
                    .subresourceRange = image.whole_image_subresource_range()
                })
            }));
        };

        // Transition ray tracing output image back to general layout
        setImageLayout(
            *storage_image,
            VK_IMAGE_LAYOUT_GENERAL);

        vk.cmdTraceRaysKHR(
            cmdbuf,
            &raygenShaderSbtEntry,
            &missShaderSbtEntry,
            &hitShaderSbtEntry,
            &callableShaderSbtEntry,
            storage_image->size().width,
            storage_image->size().height,
            1);

        /*
            Copy ray tracing output to swap chain image
        */

        // Prepare current swap chain image as transfer destination
        setImageLayout(context.image(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

        // Prepare ray tracing output image as transfer source
        setImageLayout(*storage_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);

        VkImageCopy copyRegion{};
        copyRegion.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        copyRegion.srcOffset = { 0, 0, 0 };
        copyRegion.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        copyRegion.dstOffset = { 0, 0, 0 };
        copyRegion.extent = { storage_image->size().width, storage_image->size().height, 1 };
        vkCmdCopyImage(cmdbuf, storage_image->handle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, image.handle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

        // Transition swap chain image back for presentation
        setImageLayout(
            context.image(),
            VK_IMAGE_LAYOUT_GENERAL,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    }

    void updateUniformBuffers() {
        uniformData.projInverse = invert_mat4(camera_get_proj_mat4(&camera, storage_image->size().width, storage_image->size().height));
        uniformData.viewInverse = invert_mat4(camera_get_pure_view_mat4(&camera));
        ubo->uploadDataSync(0, sizeof(uniformData), &uniformData);
    }

    void prepare() {
        // Setup vertices for a single cube
        auto cube = make_cube();

        // Setup indices
        std::vector<uint32_t> indices = { 0, 1, 2 };
        indexCount = static_cast<uint32_t>(indices.size());

        // Create buffers
        // For the sake of simplicity we won't stage the vertex data to the GPU memory
        // Vertex buffer
        vertexBuffer = std::make_unique<imr::Buffer>(*device, sizeof(cube.faces),
            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            &cube.faces);
        // Index buffer
        indexBuffer = std::make_unique<imr::Buffer>(*device, sizeof(cube.tris),
            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            &cube.tris);

        // Create the acceleration structures used to render the ray traced scene
        bottomLevelAS = std::make_unique<imr::AccelerationStructure>(*device);

        std::vector<imr::AccelerationStructure::TriangleGeometry> geometries;
        for (int i = 0; i < 6; i++) {
            // Setup identity transform matrix
            VkTransformMatrixKHR transformMatrix = {
                1.0f, 0.0f, 0.0f, 0.0f,
                0.0f, 1.0f, 0.0f, 0.0f,
                0.0f, 0.0f, 1.0f, 0 * 5.0f,
            };
            geometries.push_back({
                vertexBuffer->device_address(), indexBuffer->device_address() + i * sizeof(cube.tris[i]), 24, 2, transformMatrix
            });
        }

        bottomLevelAS->createBottomLevelAccelerationStructure(geometries);
        topLevelAS = std::make_unique<imr::AccelerationStructure>(*device);

        std::vector<std::tuple<VkTransformMatrixKHR, imr::AccelerationStructure*>> instances;
        for(int i = 0; i<3; i++){
            VkTransformMatrixKHR transformMatrix = {
                1.0f, 0.0f, 0.0f, i * 6.0f,
                0.0f, 1.0f, 0.0f, 0.0f,
                0.0f, 0.0f, 1.0f, 0.0f,
            };
            instances.emplace_back(transformMatrix, &*bottomLevelAS);
        }

        topLevelAS->createTopLevelAccelerationStructure(instances);

        imr_pipeline = std::make_unique<imr::RayTracingPipeline>(*device);
    }

    void run() {
        imr::FpsCounter fps_counter;

        auto prev_frame = imr_get_time_nano();
        float delta = 0;
        
        while (!glfwWindowShouldClose(window)) {
            fps_counter.tick();
            fps_counter.updateGlfwWindowTitle(window);

            updateUniformBuffers();

            swapchain->renderFrameSimplified([&](imr::Swapchain::SimplifiedRenderContext& context) {
                camera_update(window, &camera_input);
                camera_move_freelook(&camera, &camera_input, &camera_state, delta);

                if (!storage_image || storage_image->size().width != context.image().size().width || 
                        storage_image->size().height != context.image().size().height) {

                VkImageUsageFlagBits imageFlags = static_cast<VkImageUsageFlagBits>(VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);

                storage_image = std::make_unique<imr::Image>(*device, VK_IMAGE_TYPE_2D, context.image().size(), swapchain->format(), imageFlags);

                auto cmdbuf = context.cmdbuf();
                device->dispatch.cmdPipelineBarrier2KHR(cmdbuf, tmpPtr((VkDependencyInfo) {
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
                                    .image = storage_image->handle(),
                                    .subresourceRange = storage_image->whole_image_subresource_range()
                                    })
                            }));
                }

                draw(context);
                
                auto now = imr_get_time_nano();
                delta = ((float) ((now - prev_frame) / 1000L)) / 1000000.0f;
                prev_frame = now;

                glfwPollEvents();
            });
        }

        swapchain->drain();
    }
};

int main() {
    VulkanExample rtPipeline{};
    rtPipeline.run();
    return 0;
}
